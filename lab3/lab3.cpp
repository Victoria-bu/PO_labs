#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <functional>
#include <chrono>
#include <random>
#include <shared_mutex>
#include <condition_variable>
#include <mutex>

using namespace std;
using namespace chrono;

using read_write_lock = shared_mutex;
using read_lock = shared_lock<read_write_lock>;
using write_lock = unique_lock<read_write_lock>;

mutex cout_mutex;

// ЧЕРГА ЗАДАЧ
template <typename task_type_t>
class task_queue {
    using task_queue_implementation = queue<task_type_t>;

public:
    inline task_queue() = default;
    inline ~task_queue() { clear(); }
    
    inline task_queue(task_queue&& other) noexcept {
        write_lock lock(other.m_rw_lock);
        m_tasks = std::move(other.m_tasks);
    }
    
    inline bool empty() const;
    inline size_t size() const;
    inline void clear();
    inline bool pop(task_type_t& task);
    
    template <typename... arguments>
    inline void emplace(arguments&&... parameters);

    task_queue(const task_queue& other) = delete;
    task_queue& operator=(const task_queue& rhs) = delete;
    task_queue& operator=(task_queue&& rhs) = delete;

private:
    mutable read_write_lock m_rw_lock;
    task_queue_implementation m_tasks;
};

template <typename task_type_t>
bool task_queue<task_type_t>::empty() const {
    read_lock _(m_rw_lock);
    return m_tasks.empty();
}

template <typename task_type_t>
size_t task_queue<task_type_t>::size() const {
    read_lock _(m_rw_lock);
    return m_tasks.size();
}

template <typename task_type_t>
void task_queue<task_type_t>::clear() {
    write_lock _(m_rw_lock);
    while (!m_tasks.empty()) {
        m_tasks.pop();
    }
}

template <typename task_type_t>
bool task_queue<task_type_t>::pop(task_type_t& task) {
    write_lock _(m_rw_lock);
    if (m_tasks.empty()) {
        return false;
    } else {
        task = std::move(m_tasks.front());
        m_tasks.pop();
        return true;
    }
}

template <typename task_type_t>
template <typename... arguments>
void task_queue<task_type_t>::emplace(arguments&&... parameters) {
    write_lock _(m_rw_lock);
    m_tasks.emplace(std::forward<arguments>(parameters)...);
}

class thread_pool;

// СТРУКТУРА ДЛЯ СТАТИСТИКИ
struct ThreadPoolStats {
    size_t total_threads_created = 0;
    
    atomic<long long> total_wait_time_us{0};
    atomic<size_t> wait_samples{0};
    
    atomic<long long> queue0_length_sum{0};
    atomic<long long> queue1_length_sum{0};
    atomic<long long> queue2_length_sum{0};
    atomic<size_t> queue_samples{0};
    
    atomic<long long> total_execution_time_us{0};
    atomic<size_t> completed_tasks{0};
    
    double get_avg_wait_time_ms() const {
        if (wait_samples == 0) return 0;
        return (total_wait_time_us / 1000.0) / wait_samples;
    }
    
    double get_avg_queue0_length() const {
        if (queue_samples == 0) return 0;
        return static_cast<double>(queue0_length_sum) / queue_samples;
    }
    
    double get_avg_queue1_length() const {
        if (queue_samples == 0) return 0;
        return static_cast<double>(queue1_length_sum) / queue_samples;
    }
    
    double get_avg_queue2_length() const {
        if (queue_samples == 0) return 0;
        return static_cast<double>(queue2_length_sum) / queue_samples;
    }
    
    double get_avg_execution_time_ms() const {
        if (completed_tasks == 0) return 0;
        return (total_execution_time_us / 1000.0) / completed_tasks;
    }
    
    void print() {
        cout << "\nСТАТИСТИКА\n";
        cout << "Кількість створених потоків: " << total_threads_created << "\n";
        cout << "Середній час очікування: " << get_avg_wait_time_ms()/1000 << " с\n";
        cout << "Середня довжина черги 0: " << get_avg_queue0_length() << "\n";
        cout << "Середня довжина черги 1: " << get_avg_queue1_length() << "\n";
        cout << "Середня довжина черги 2: " << get_avg_queue2_length() << "\n";
        cout << "Середній час виконання задач: " << get_avg_execution_time_ms() << " мс\n";
    }
};

// ПУЛ ПОТОКІВ
class thread_pool {
public:
    inline thread_pool() = default;
    inline ~thread_pool() { terminate(); }

public:
    void initialize(const size_t queues_count, const size_t workers_per_queue);
    void terminate(bool wait_for_tasks = true);
    void routine(size_t queue_index);
    bool working() const;
    bool working_unsafe() const;
    
    template <typename task_t, typename... arguments>
    void add_task(task_t&& task, arguments&&... parameters);
    
    ThreadPoolStats stats;

public:
    thread_pool(const thread_pool& other) = delete;
    thread_pool(thread_pool&& other) = delete;
    thread_pool& operator=(const thread_pool& rhs) = delete;
    thread_pool& operator=(thread_pool&& rhs) = delete;

    size_t get_queue_size(size_t idx) const;
    size_t get_total_tasks() const { return total_tasks_added; }
    size_t get_completed_tasks() const { return total_tasks_completed; }
    
private:
    mutable read_write_lock m_rw_lock;
    mutable condition_variable_any m_task_waiter;
    
    vector<thread> m_workers;
    vector<task_queue<function<void()>>> m_queues;
    
    bool m_initialized = false;
    bool m_terminated = false;
    
    atomic<size_t> total_tasks_added{0};
    atomic<size_t> total_tasks_completed{0};
    
    size_t select_least_loaded_queue() const;
};

thread_local time_point<steady_clock> wait_start;

bool thread_pool::working() const {
    read_lock _(m_rw_lock);
    return working_unsafe();
}

bool thread_pool::working_unsafe() const {
    return m_initialized && !m_terminated;
}

void thread_pool::initialize(const size_t queues_count, const size_t workers_per_queue) {
    write_lock _(m_rw_lock);
    
    if (m_initialized || m_terminated) {
        return;
    }
    
    m_queues.reserve(queues_count);
    for (size_t i = 0; i < queues_count; i++) {
        m_queues.emplace_back();
    }
    
    for (size_t q = 0; q < queues_count; q++) {
        for (size_t w = 0; w < workers_per_queue; w++) {
            m_workers.emplace_back(&thread_pool::routine, this, q);
            stats.total_threads_created++;
        }
    }
    
    m_initialized = !m_workers.empty();
}

void thread_pool::routine(size_t queue_index) {
    auto& my_queue = m_queues[queue_index];
    
    while (true) {
        wait_start = steady_clock::now();
        
        bool task_acquired = false;
        function<void()> task;
        
        {
            write_lock _(m_rw_lock);
            
            auto wait_condition = [this, &my_queue, &task_acquired, &task] {
                task_acquired = my_queue.pop(task);
                return m_terminated || task_acquired;
            };
            
            m_task_waiter.wait(_, wait_condition);
        }
        
        auto wait_end = steady_clock::now();
        auto wait_time = duration_cast<microseconds>(wait_end - wait_start).count();
        stats.total_wait_time_us += wait_time;
        stats.wait_samples++;
        
        if (m_terminated && !task_acquired) {
            return;
        }
        
        if (task_acquired) {
            auto exec_start = steady_clock::now();
            task();
            auto exec_end = steady_clock::now();
            
            auto exec_time = duration_cast<microseconds>(exec_end - exec_start).count();
            stats.total_execution_time_us += exec_time;
            stats.completed_tasks++;
            
            total_tasks_completed++;
        }
    }
}

size_t thread_pool::select_least_loaded_queue() const {
    size_t min_size = numeric_limits<size_t>::max();
    size_t selected = 0;
    
    for (size_t i = 0; i < m_queues.size(); i++) {
        size_t qsize = m_queues[i].size();
        if (qsize < min_size) {
            min_size = qsize;
            selected = i;
        }
    }
    return selected;
}

template <typename task_t, typename... arguments>
void thread_pool::add_task(task_t&& task, arguments&&... parameters) {
    {
        read_lock _(m_rw_lock);
        if (!working_unsafe()) {
            return;
        }
    }
    
    auto bind = std::bind(std::forward<task_t>(task), std::forward<arguments>(parameters)...);
    size_t queue_idx = select_least_loaded_queue();
    m_queues[queue_idx].emplace(bind);
    
    total_tasks_added++;
    m_task_waiter.notify_one();
}

void thread_pool::terminate(bool wait_for_tasks) {
    {
        write_lock _(m_rw_lock);
        if (!m_initialized) return;
        
        if (!wait_for_tasks) {
            for (auto& q : m_queues) q.clear();
        }
        
        m_terminated = true;
    }
    
    m_task_waiter.notify_all();
    
    for (thread& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
    
    m_workers.clear();
    m_queues.clear();
    m_initialized = false;
    m_terminated = false;
}

size_t thread_pool::get_queue_size(size_t idx) const {
    read_lock _(m_rw_lock);
    if (idx >= m_queues.size()) return 0;
    return m_queues[idx].size();
}

// ТЕСТОВА ПРОГРАМА
int random_int(int min, int max) {
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dist(min, max);
    return dist(gen);
}

void test_task(int id, int producer_id) {
    int exec_time = random_int(8000, 14000);
    
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[Pro" << producer_id << "] Задача " << id << " почалась (" << exec_time/1000 << " сек)\n";
    }
    
    this_thread::sleep_for(chrono::milliseconds(exec_time));
    
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[Pro" << producer_id << "] Задача " << id << " завершилась\n";
    }
}

void producer(thread_pool& pool, int id, int tasks_count, atomic<bool>& running) {
    for (int i = 0; i < tasks_count && running; i++) {
        pool.add_task(test_task, i, id);
        int delay = random_int(500, 2000);
        this_thread::sleep_for(chrono::milliseconds(delay));
    }
    
    lock_guard<mutex> lock(cout_mutex);
    cout << "--->(Продюсер " << id << " завершив роботу)\n";
}

void monitor(thread_pool& pool, atomic<bool>& running) {
    while (running) {
        this_thread::sleep_for(chrono::seconds(3));
        
        pool.stats.queue0_length_sum += pool.get_queue_size(0);
        pool.stats.queue1_length_sum += pool.get_queue_size(1);
        pool.stats.queue2_length_sum += pool.get_queue_size(2);
        pool.stats.queue_samples++;
        
        lock_guard<mutex> lock(cout_mutex);
        cout << "\n----------------------------МОНІТОР\n";
        cout << "Черги: Q0=" << pool.get_queue_size(0) 
             << " Q1=" << pool.get_queue_size(1) 
             << " Q2=" << pool.get_queue_size(2) << "\n";
        cout << "Всього додано: " << pool.get_total_tasks()
             << ", виконано: " << pool.get_completed_tasks() << "\n";
        cout << "---------------------------------------\n\n";
    }
}

int main() {
    auto test_start = steady_clock::now();
    
    thread_pool pool;
    pool.initialize(3, 2);
    
    atomic<bool> running{true};
    
    thread monitor_thread(monitor, ref(pool), ref(running));
    
    vector<thread> producers;
    int producers_count = 5;
    int tasks_per_producer = 5;
    
    cout << "Запускаємо " << producers_count << " продюсерів...\n\n";
    
    for (int i = 0; i < producers_count; i++) {
        producers.emplace_back(producer, ref(pool), i, tasks_per_producer, ref(running));
    }
    
    this_thread::sleep_for(chrono::seconds(30));
    
    cout << "\n>>> ЗАВЕРШЕННЯ ТЕСТУВАННЯ <<<\n";
    running = false;
    pool.terminate(true);
    
    for (auto& p : producers) {
        if (p.joinable()) p.join();
    }
    
    monitor_thread.join();
    
    auto test_end = steady_clock::now();
    auto test_duration = duration_cast<seconds>(test_end - test_start).count();
    
    cout << "\nРЕЗУЛЬТАТИ ТЕСТУВАННЯ\n";
    cout << "Тривалість тесту: " << test_duration << " секунд\n\n";
    pool.stats.print();
    cout << "Всього додано задач: " << pool.get_total_tasks() << "\n";
    cout << "Всього виконано задач: " << pool.get_completed_tasks() << "\n";
    
    return 0;
}