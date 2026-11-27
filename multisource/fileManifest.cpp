#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <cmath>

using namespace std;
#include "fileManifest.h"
// struct FileManifest {
//     string name; 
//     uint64_t filesize;  //total size of file without chunks
//     uint32_t chunksize; //lets say 64KB for now
//     uint32_t num_chunk; //(fsize/chsize)
//     vector<string> chunkhashes; //checking hash of each chunk.

//     uint32_t chunklength(uint32_t chunkindex) const{
//         //early error check to handle invlaid entries.
//         assert(chunkindex < num_chunk);
//         if (chunkindex < num_chunk - 1) {
//             return chunksize;
//         } 
//         return filesize - (uint64_t)chunksize * (num_chunk - 1);
//     }
// };
// //int PeerId;

//tests
int main(){
    FileManifest fm;
    fm.filesize = 2000000; // 2MB
    fm.chunksize = 65536; // 64KB
    fm.num_chunk = (fm.filesize + fm.chunksize - 1) / fm.chunksize; //basically ceil function the 
    // the ceil fn isnt ideal for large floats (can misbehave) this design change was suggested by AI.
   
    //hardcoding file chunk division (done in file distribution part) for tests
    vector<vector<int>> chunk_holders;
    for (int i = 0; i < fm.num_chunk; ++i) {
        vector<int> holders;
        for (int j = 0; j < 2; ++j) {
            holders.push_back((i + j) % 4); //just a simple pattern for testing
        }
        chunk_holders.push_back(holders);
    }

    // fm.chunklength(25);
    // fm.chunklength(30);
    //fm.chunklength(31); //error check this chunk doesnt exist.
    uint64_t fsize = 0; //not equating to 0 will give the value + whatever was in the garbage.
    for (uint32_t i = 0; i < fm.num_chunk; ++i) {
        cout << "Chunk " << i << " length: " << fm.chunklength(i) << endl;
        fsize += fm.chunklength(i);
    }
    assert(fsize == fm.filesize);

    //show chunk holders for each chunk
    cout << "Total file size: " << fsize << endl;
    for (int i = 0; i < fm.num_chunk; ++i) {
        cout << "Chunk " << i << " is held by peers: ";
        for (int peer : chunk_holders[i]) {
            cout << peer << " ";
        }
        cout << endl;
    }
}