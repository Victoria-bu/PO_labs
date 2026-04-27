#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

enum : uint32_t {
    CMD_SEND_MATRIX = 1,
    CMD_START_THREADS = 2,
    CMD_RESULT = 3,        
    CMD_ACK = 4,
    CMD_ERROR = 5,
    CMD_GET_STATUS = 6,
    CMD_STATUS = 7
};

ssize_t read_n(int fd, void* buf, size_t n) {
    char* p = (char*)buf;
    size_t left = n;
    while (left) {
        ssize_t r = read(fd, p, left);
        if (r <= 0) return r;
        p += r; left -= r;
    }
    return n;
}

ssize_t write_n(int fd, const void* buf, size_t n) {
    const char* p = (const char*)buf;
    size_t left = n;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) return w;
        p += w; left -= w;
    }
    return n;
}

void send_error(int fd, const std::string& msg) {
    uint32_t cmd = htonl(CMD_ERROR), len = htonl((uint32_t)msg.size());
    write_n(fd, &cmd, 4); write_n(fd, &len, 4);
    if (!msg.empty()) write_n(fd, msg.data(), msg.size());
}

void send_ack(int fd) {
    uint32_t cmd = htonl(CMD_ACK), len = htonl(0);
    write_n(fd, &cmd, 4); write_n(fd, &len, 4);
}

void handle_client(int client_fd) {
    std::string prefix = "[CLIENT " + std::to_string(client_fd) + "] ";
    std::cout << prefix << "connected\n";

    uint32_t n = 0;
    std::vector<int32_t> matrix;
    std::vector<int32_t> result_matrix;
    
    // Статус: 0 - очікування, 1 - в процесі, 2 - готово
    std::atomic<int> status{0};
    uint32_t elapsed_ms = 0;

    while (true) {
        uint32_t cmd_net, len_net;
        if (read_n(client_fd, &cmd_net, 4) <= 0) break;
        if (read_n(client_fd, &len_net, 4) <= 0) break;
        
        uint32_t cmd = ntohl(cmd_net);
        uint32_t len = ntohl(len_net);

        if (cmd == CMD_SEND_MATRIX) {
            uint32_t n_net;
            read_n(client_fd, &n_net, 4);
            n = ntohl(n_net);
            
            size_t expect = (size_t)n * n;
            matrix.assign(expect, 0);
            for (size_t i = 0; i < expect; i++) {
                int32_t vnet;
                read_n(client_fd, &vnet, 4);
                matrix[i] = ntohl(vnet);
            }
            
            status = 0; // Скидаємо статус для нової матриці
            std::cout << prefix << "received matrix " << n << "x" << n << "\n";
            send_ack(client_fd);

        } else if (cmd == CMD_START_THREADS) {
            uint32_t thr_net;
            read_n(client_fd, &thr_net, 4);
            uint32_t threads = ntohl(thr_net);
            if (threads == 0) threads = 1;

            std::cout << prefix << "starting computation (threads=" << threads << ")\n";
            
            // ПЕРЕХІД В АСИНХРОННИЙ РЕЖИМ
            status = 1; 
            send_ack(client_fd); // Одразу кажемо клієнту "процес пішов"

            // Запускаємо розрахунки у фоновому потоці, щоб не блокувати цикл команд
            std::thread([&status, &matrix, &result_matrix, &elapsed_ms, n, threads, prefix]() {
                auto t0 = std::chrono::steady_clock::now();
                
                std::vector<int32_t> local_result = matrix;
                std::vector<std::thread> workers;
                size_t rows_per = n / threads;
                size_t extra = n % threads;
                size_t current_row = 0;

                for (uint32_t t = 0; t < threads; t++) {
                    size_t start = current_row;
                    size_t count = rows_per + (t < extra ? 1 : 0);
                    current_row += count;
                    size_t end = start + count;

                    workers.emplace_back([start, end, n, &matrix, &local_result]() {
                        for (size_t i = start; i < end; i++) {
                            long long row_sum = 0;
                            for (size_t j = 1; j < (size_t)n; j += 2) {
                                row_sum += matrix[i * n + j];
                            }
                            local_result[i * n + i] = (int32_t)row_sum;
                        }
                    });
                }

                for (auto& w : workers) w.join();
                
                auto t1 = std::chrono::steady_clock::now();
                elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                
                result_matrix = std::move(local_result);
                status = 2; // Позначаємо, що готово
                std::cout << prefix << "computation finished in " << elapsed_ms << "ms\n";
            }).detach();

        } else if (cmd == CMD_GET_STATUS) {
            // Відповідаємо поточним статусом (0, 1 або 2)
            uint32_t s_cmd = htonl(CMD_STATUS);
            uint32_t s_len = htonl(4);
            uint32_t s_val = htonl((uint32_t)status.load());
            write_n(client_fd, &s_cmd, 4);
            write_n(client_fd, &s_len, 4);
            write_n(client_fd, &s_val, 4);

        } else if (cmd == CMD_RESULT) {
            if (status != 2) {
                send_error(client_fd, "Result not ready. Check status first.");
            } else {
                // Відправляємо результат
                uint32_t r_cmd = htonl(CMD_RESULT);
                uint32_t payload_len = 4 + 4 + n * n * 4;
                uint32_t r_len = htonl(payload_len);
                uint32_t r_ms = htonl(elapsed_ms);
                uint32_t r_n = htonl(n);
                
                write_n(client_fd, &r_cmd, 4);
                write_n(client_fd, &r_len, 4);
                write_n(client_fd, &r_ms, 4);
                write_n(client_fd, &r_n, 4);
                
                for (int32_t val : result_matrix) {
                    int32_t v_net = htonl(val);
                    write_n(client_fd, &v_net, 4);
                }
                std::cout << prefix << "result matrix sent\n";
            }
        } else {
            // Скидаємо невідомі дані
            if (len > 0) {
                std::vector<char> junk(len);
                read_n(client_fd, junk.data(), len);
            }
            send_error(client_fd, "Unknown command");
        }
    }

    close(client_fd);
    std::cout << prefix << "disconnected\n";
}

int main() {
    const int PORT = 8080;
    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(srv_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    listen(srv_fd, 10);
    std::cout << "[SERVER] Started on port " << PORT << ". Waiting for clients...\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv_fd, (sockaddr*)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            // Кожен клієнт обробляється в окремому потоці
            std::thread(handle_client, client_fd).detach();
        }
    }

    close(srv_fd);
    return 0;
}