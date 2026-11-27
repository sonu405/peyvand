#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include "Work_Queue.h"   
#include <chrono>
#include<cstdint>

using namespace std;

// chunk i is held by peers i%4 and (i+1)%4
// getting list of holders
vector<vector<int>> make_holders(uint32_t num_chunks) {
    vector<vector<int>> holders;
    for (uint32_t i = 0; i < num_chunks; ++i) {
        holders.push_back({ (int)(i % 4), (int)((i + 1) % 4) });
    }
    return holders;
}
void test_single_thread(){
    const uint32_t N = 31;
    auto holders = make_holders(N);
    ChunkQueue q(N, holders);

    // Test taking chunks for each peer
    vector<uint32_t> taken_chunks;
    while (auto chunk = q.take(0)) {
        taken_chunks.push_back(*chunk);
    }
    for (uint32_t c: taken_chunks) {
        cout << "Peer 0 took chunk: " << c << endl;
    }
    cout << "\n";
    assert (taken_chunks.size() == 15); // Peer 0 should take half of the chunks
    assert(!q.take(0).has_value()); // No more chunks for peer 0

    //make chunks available again for peer 0
    q.put_back(4);
    auto put_back = q.take(0);
    assert(put_back.has_value() && *put_back == 4); // Peer 0 should take chunk 4 again
    // Expected order: 0 3 4 7 8 11 12 15 16 19 20 23 24 27 28
}

void test_multi_thread() {
    const uint32_t N = 31;
    auto holders = make_holders(N);
    ChunkQueue q(N, holders);

    vector<thread> threads;
    vector<vector<uint32_t>> results(4); // one vector per peer

    for (int peer = 0; peer < 4; ++peer) {
        threads.emplace_back([peer, &q, &results]() {
            while (auto chunk = q.take(peer)) {
                results[peer].push_back(*chunk);
                this_thread::sleep_for(chrono::milliseconds(5)); // pretend to download
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int peer = 0; peer < 4; ++peer) {
        cout << "Peer " << peer << " took chunks: ";
        for (uint32_t c : results[peer]) {
            cout << c << " ";
        }
        cout << "\n";
    }

    // Every chunk must be taken exactly once.
    vector<int> counts(N, 0);
    for (int peer = 0; peer < 4; ++peer) {
        for (uint32_t c : results[peer]) {
            counts[c]++;
        }
    }
    for (uint32_t i = 0; i < N; ++i) {
        assert(counts[i] == 1);
    }

    cout << "multi-thread test passed\n";
}

int main() {
    test_single_thread();
    test_multi_thread();
    cout << "single-thread test passed\n";
    return 0;
}