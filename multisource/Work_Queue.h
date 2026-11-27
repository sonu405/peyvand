//works perfectly fine
#pragma once
#include<deque>
#include<mutex>
#include<optional>
#include<algorithm>
#include<vector>
#include<cstdint>

using namespace std;

class ChunkQueue{
private:
    deque<uint32_t> queue_;
    mutex mutex_;
    const vector<vector<int>>& holders_; // Reference to the chunk holders
public:
    ChunkQueue(uint32_t num_chunks, const vector<vector<int>>& holders): holders_(holders) {
        for (uint32_t i = 0; i < num_chunks; ++i) {
            queue_.push_back(i);
        }
    }

    // return chunk i that peer holds
    // this has a edge case where if one thread takes the chubnk out and another 
    // peer thread searches for that chunk it will return nullopt.
    // which will cause it to exit before file is even downlaoded.
    optional<uint32_t> take(int peer){
        lock_guard<mutex> lock(mutex_);
        for (auto i = queue_.begin(); i != queue_.end(); ++i) {
            uint32_t chunk_index = *i;
            const auto& chunk_holders = holders_[chunk_index];
            if (find(chunk_holders.begin(), chunk_holders.end(), peer) != chunk_holders.end()) {
                queue_.erase(i);
                return chunk_index;
            }
        }
        return nullopt; // No available chunk for this peer
    }

    void put_back(uint32_t chunk){
        lock_guard<mutex> lock(mutex_);
        queue_.push_back(chunk);
    }

};
