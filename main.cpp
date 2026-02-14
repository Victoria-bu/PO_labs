#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>

using namespace std;
using namespace chrono;

volatile long long checksum = 0; // захист від оптимізації

vector<vector<int>> generateMatrix(int n) {
    vector<vector<int>> matrix(n, vector<int>(n));
    mt19937 gen(19);
    uniform_int_distribution<> dist(1, 100);

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            matrix[i][j] = dist(gen);

    return matrix;
}


void processMatrixSequential(vector<vector<int>>& matrix) {
    int n = matrix.size();

    for (int i = 0; i < n; i++) {
        long long sum = 0;

        for (int j = 1; j < n; j += 2)
            sum += matrix[i][j];

        matrix[i][i] = sum;
        checksum += sum;
    }
}


void processParts(vector<vector<int>>& matrix, int startRow, int endRow, long long& result) {
    int n = matrix.size();
    long long localSum = 0;

    for (int i = startRow; i < endRow; i++) {
        long long sum = 0;

        for (int j = 1; j < n; j += 2)
            sum += matrix[i][j];

        matrix[i][i] = sum;
        localSum += sum;
    }
    
    result = localSum;
}

void processMatrixParallel(vector<vector<int>>& matrix, int threadsNum) {
    int n = matrix.size();
    vector<thread> threads;
    vector<long long> result(threadsNum, 0);

    int rowsForThread = n / threadsNum;
    int zalyshok = n % threadsNum;
    int current = 0;

    for (int t = 0; t < threadsNum; t++) {
        int start = current;
        int end = start + rowsForThread + (t < zalyshok ? 1 : 0);
        threads.emplace_back(processParts, ref(matrix), start, end, ref(result[t]));
        current = end;
    }

    for (auto& th : threads){
        th.join();
    }

    long long total = 0;
    for (int t = 0; t < threadsNum; t++){
        total += result[t];
    }
    checksum = total;
}

bool compareMatrix(const vector<vector<int>>& a, const vector<vector<int>>& b) {
    int n = a.size();
    for (int i = 0; i < n; i++)
        if (a[i][i] != b[i][i])
            return false;
    return true;
}

int main() 
{
    vector<int> sizes = {200, 1000, 4000, 10000, 20000, 50000};
    vector<int> testThreads = {6, 12, 24, 48, 96, 192};

    for (int n : sizes) {
        auto original = generateMatrix(n);
        cout << "Matrix size: " << n << "x" << n << "\n";

        //SEQUENTIAL (послідовний алгоритм)
        auto matrix = original;

        auto start = high_resolution_clock::now();
        processMatrixSequential(matrix);
        auto end = high_resolution_clock::now();

        double seqTime = duration<double>(end - start).count();

        cout << "Threads:    1 | Time:" << fixed << setprecision(6)
             << seqTime << " sec\n";

        //PARALLEL (алгоритм з паралелізацією)
        for (int threads : testThreads) {
            auto matrixP = original;

            auto startP = high_resolution_clock::now();
            processMatrixParallel(matrixP, threads);
            auto endP = high_resolution_clock::now();

            double parTime = duration<double>(endP - startP).count();

            bool correct = compareMatrix(matrix, matrixP);
            double speedup = seqTime / parTime;

             cout << "Threads:  " << setw(3) << threads
                 << " | Time: " << fixed << setprecision(6) << parTime << " sec"
                 << " | Speedup: " << fixed << setprecision(2) << speedup
                 << "x | Correct: " << (correct ? "yes" : "no") << "\n";
        }
        cout << endl;
    }

    return 0;
}