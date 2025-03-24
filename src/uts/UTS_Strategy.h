#include <openssl/sha.h>
#include <vector>
#include <math.h>
#include <iostream>

class Communication_Hash;

class UTS_Strategy {
public:
    int size;
    std::vector<int> depth;
    std::vector<unsigned char> hash;
    std::vector<int> lower;
    std::vector<int> upper;
    double den;

    UTS_Strategy(int initDepth, double den);

    explicit UTS_Strategy(double den);

    void expand(SHA_CTX &ctx);

    long getResult();

    void seed(int seed, int d);

    void process(Communication_Hash &communication);

    void merge(UTS_Strategy b);

    UTS_Strategy split();

    bool checkIsSplittable();

    UTS_Strategy splitSub(int from, int to);

    void addCount(long newCount);

    void clear();

private:
    SHA_CTX ctx;
    long count;

    void digest(int d, SHA_CTX &ctx);

    void grow();
};
