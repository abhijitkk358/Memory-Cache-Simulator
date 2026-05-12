#include <iostream>
#include <vector>

using namespace std;



const int N = 16; 

int main() {


    vector<vector<int>> A(N, vector<int>(N, 1));
    vector<vector<int>> B(N, vector<int>(N, 2));
    vector<vector<int>> C(N, vector<int>(N, 0));



    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            for (int k = 0; k < N; ++k) {

                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    cout << "Test completed successfully! Sample result: " << C[0][0] << endl;
    
    return 0;
}