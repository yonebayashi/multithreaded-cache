#include <random>
#include <iostream>
#include <string>
#include <chrono>
#include <fstream>
#include <thread>
#include <mutex>

#include "cache.hh"
#include "evictor.hh"

#define LOCALHOST_ADDRESS "127.0.0.1"
#define PORT "8080"
#define NUM_THREADS 3

int MIN_KEY_SIZE = 1;
int MAX_KEY_SIZE = 250;
int MIN_VAL_SIZE = 1;
int MAX_VAL_SIZE = 10000;
int NUM_REQUESTS = 1000;
int WARMUP_STEPS = 100;


static const std::string LETTERS = "abcdefghijklmnopqrstuvwxyz";
int stringLength = sizeof(LETTERS) - 1;

char string_generator(const int& seed)
{
  std::default_random_engine gen(seed);
  std::uniform_int_distribution<int> dist(0, stringLength);
  return LETTERS[dist(gen)];
}


class Generator {
    std::default_random_engine generator;
    std::extreme_value_distribution<double> distribution;
    int min;
    int max;
public:
    Generator(const double& a, const double& b, const int& min, const int& max, const long& seed):
        generator(seed), distribution(a, b), min(min), max(max)  {}

    int operator ()() {
        while (true) {
            double number = this->distribution(generator);
            if (number >= this->min && number <= this->max)
                return static_cast<int>(number);
        }
    }
};



int workload_generator(std::string& key, std::string& val) {
  typedef std::chrono::system_clock Time;
  long seed = Time::now().time_since_epoch().count();

  std::default_random_engine gen(seed);
  std::discrete_distribution<int> dist{68, 17, 15};
  auto req_type = dist(gen);

  Generator KeyGen(30.0, 8.0, MIN_KEY_SIZE, MAX_KEY_SIZE, seed);
  Generator ValGen(10.0, 50.0, MIN_VAL_SIZE, MAX_VAL_SIZE, seed);

  int key_size = KeyGen();
  for (int i = 0; i < key_size; i++) {
    key += string_generator(key_size + i);
  }

  int val_size = ValGen();
  for (int i = 0; i < val_size; i++) {
    val += string_generator(val_size + i);
  }

  return req_type;
}


void baseline_latencies(Cache& cache, Cache::size_type& size, double* measurements, const int& nreq)
{
  typedef std::chrono::system_clock Time;

  for (int i = 0; i < nreq; i++) {
    key_type key;
    std::string val;
    auto req_type = workload_generator(key, val);
    Cache::val_type pval = val.c_str();

    auto t1 = Time::now();

    if (req_type == 0) {
      cache.get(key, size);
    } else if (req_type == 1) {
      cache.set(key, pval, strlen(pval)+1);
    } else {
      cache.del(key);
    }

    auto t2 = Time::now();
    auto t = std::chrono::duration<double, std::milli>(t2 - t1);
    measurements[i] = t.count();
  }
  return;
}


void task(std::mutex& mtx, std::vector<double>& res)
{
  Cache cache(LOCALHOST_ADDRESS, PORT);
  Cache::size_type size;

  double* latencies = new double[NUM_REQUESTS];
  baseline_latencies(cache, size, latencies, NUM_REQUESTS);

  {
    std::lock_guard guard(mtx);
    for (int i = 0; i < NUM_REQUESTS; i++) {
      res.push_back(latencies[i]);
    }
  }

  delete[] latencies;
}


void threaded_performance()
{
  std::mutex mtx;
  std::vector<double> res;
  std::thread threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++)
      threads[i] = std::thread(task, std::ref(mtx), std::ref(res));

  for (auto& th : threads) th.join();


  double p95;
  double mean;

  std::sort(res.begin(), res.end());
  p95 = res[std::round(.95 * res.size())];

  double sum = 0.0;
  double total_elapsed = std::accumulate(res.begin(), res.end(), sum);
  total_elapsed *= 0.001;   // get total elapsed time (seconds)
  // std::cout << "Total elasped: " << total_elapsed << 's\n';
  mean = NUM_REQUESTS / (double) total_elapsed;   // compute mean throughput (requests/sec)

  std::cout << "95 percentile latency: " << p95 << '\n';
  std::cout << "Mean throughput: " << mean << '\n';
}

int main()
{
  Cache cache(LOCALHOST_ADDRESS, PORT);

  // ==================== WARMUP =======================
  for (int i = 0; i < WARMUP_STEPS; i++) {
    key_type key;
    std::string val;
    workload_generator(key, val);
    Cache::val_type pval = val.c_str();

    cache.set(key, pval, strlen(pval)+1);
  }

  std::cout << "Space used (after warmup): " << cache.space_used() << '\n';

  // ================ MULTITHREADING ===================

  threaded_performance();

  std::cout << "Space used (final): " << cache.space_used() << '\n';

  return 0;
}
