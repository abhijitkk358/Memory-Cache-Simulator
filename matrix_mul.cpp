#include <iostream>
#include <vector>

using namespace std;



const int N = 8; 

int main() {

    
    // We use std::vector to allocate on the heap and prevent stack overflow
    vector<vector<int>> A(N, vector<int>(N, 1));
    vector<vector<int>> B(N, vector<int>(N, 2));
    vector<vector<int>> C(N, vector<int>(N, 0));



    // Classic O(N^3) Naive Matrix Multiplication
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            for (int k = 0; k < N; ++k) {
                // This single line generates a massive amount of trace data:
                // 1. Read A[i][k]
                // 2. Read B[k][j]
                // 3. Read C[i][j]
                // 4. Write C[i][j]
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    // Print a single value to prevent the compiler from optimizing the loops away
    cout << "Test completed successfully! Sample result: " << C[0][0] << endl;
    
    return 0;
}
