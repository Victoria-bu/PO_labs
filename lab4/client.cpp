#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
#include <thread>
#include <string>

using namespace std;
using namespace chrono;

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


bool wait_for_ack(int sock) {
    uint32_t cmd_net, len_net;
    if (read_n(sock, &cmd_net, 4) <= 0) return false;
    if (read_n(sock, &len_net, 4) <= 0) return false;
    
    uint32_t cmd = ntohl(cmd_net);
    uint32_t len = ntohl(len_net);
    
    if (cmd == CMD_ERROR) {
        string msg(len, '\0');
        if (len > 0) read_n(sock, &msg[0], len);
        cerr << "[CLIENT] Server error: " << msg << "\n";
        return false;
    }
    return cmd == CMD_ACK;
}

int main() {
    const char* HOST = "127.0.0.1";
    const int PORT = 8080;
    const uint32_t N = 5000;                   
    vector<uint32_t> threads_list = {1, 6, 12, 24, 48}; 

   
    cout << "[CLIENT] generating " << N << "x" << N << " matrix... ";
    vector<int32_t> matrix(N * N);
    mt19937 gen(steady_clock::now().time_since_epoch().count());
    uniform_int_distribution<int32_t> dist(0, 100);
    for (auto& v : matrix) {
        v = dist(gen);
    }
    cout << "Done.\n";

   
    cout << "[CLIENT] connecting to " << HOST << ":" << PORT << "... ";
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &srv.sin_addr);
    
    if (connect(sock, (sockaddr*)&srv, sizeof(srv)) < 0) {
        cerr << "Connection failed!\n";
        return 1;
    }
    cout << "Connected!\n";


    cout << "[CLIENT] sending matrix... ";
    uint32_t cmd = htonl(CMD_SEND_MATRIX);
    uint32_t len = htonl(4 + N * N * 4);
    uint32_t nn = htonl(N);
    
    write_n(sock, &cmd, 4);
    write_n(sock, &len, 4);
    write_n(sock, &nn, 4);
    
    for (int32_t v : matrix) {
        int32_t net = htonl(v);
        write_n(sock, &net, 4);
    }
    
    if (wait_for_ack(sock)) {
        cout << "Server accepted matrix (ACK).\n";
    } else {
        return 1;
    }

    cout << "\n[CLIENT] Starting computations:\n";
    for (uint32_t thr : threads_list) {
        cout << "  > Threads: " << thr << " ... ";

        // Початок обчислень
        cmd = htonl(CMD_START_THREADS);
        len = htonl(4);
        uint32_t thr_net = htonl(thr);
        write_n(sock, &cmd, 4);
        write_n(sock, &len, 4);
        write_n(sock, &thr_net, 4);

        // Чекаємо підтвердження старту
        if (!wait_for_ack(sock)) {
            cerr << "Failed to start processing.\n";
            break;
        }

        // Опитування статусу
        while (true) {
            this_thread::sleep_for(milliseconds(100));
            
            uint32_t c_stat = htonl(CMD_GET_STATUS);
            uint32_t l_stat = htonl(0);
            write_n(sock, &c_stat, 4);
            write_n(sock, &l_stat, 4);

            uint32_t r_cmd, r_len, r_status;
            read_n(sock, &r_cmd, 4);
            read_n(sock, &r_len, 4);
            read_n(sock, &r_status, 4);

            if (ntohl(r_cmd) == CMD_STATUS) {
                uint32_t current_status = ntohl(r_status);
                if (current_status == 2) { // 2 = Готово
                    break;
                }
            }
        }

        // Запитуємо результат 
        uint32_t c_res = htonl(CMD_RESULT);
        uint32_t l_res = htonl(0);
        write_n(sock, &c_res, 4);
        write_n(sock, &l_res, 4);

        uint32_t rc, rl;
        read_n(sock, &rc, 4);
        read_n(sock, &rl, 4);
        
        if (ntohl(rc) == CMD_RESULT) {
            uint32_t r_ms_net, r_n_net;
            read_n(sock, &r_ms_net, 4);
            read_n(sock, &r_n_net, 4);
            
            uint32_t elapsed_ms = ntohl(r_ms_net);
            uint32_t r_n = ntohl(r_n_net);
            
            size_t total = (size_t)r_n * r_n;
            for (size_t i = 0; i < total; i++) {
                int32_t dummy;
                read_n(sock, &dummy, 4);
            }
            
            double seconds = elapsed_ms / 1000.0;
            cout << "Done in " << seconds << " s\n";
        }
        
        this_thread::sleep_for(milliseconds(200));
    }

    cout << "\n[CLIENT] All tests finished. Disconnecting.\n";
    close(sock);
    return 0;
}
