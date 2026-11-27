#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <cmath>

using namespace std;

struct FileManifest {
    string name; 
    uint64_t filesize;  //total size of file without chunks
    uint32_t chunksize; //lets say 64KB for now
    uint32_t num_chunk; //(fsize/chsize)
    vector<string> chunkhashes; //checking hash of each chunk.

    uint32_t chunklength(uint32_t chunkindex) const{
        //early error check to handle invlaid entries.
        assert(chunkindex < num_chunk);
        if (chunkindex < num_chunk - 1) {
            return chunksize;
        } 
        return filesize - (uint64_t)chunksize * (num_chunk - 1);
    }
};