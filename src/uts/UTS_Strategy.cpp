/*
 * Copyright (c) 2025 Wagomu project.
 *
 * This program and the accompanying materials are made available to you under
 * the terms of the Eclipse Public License 1.0 which accompanies this
 * distribution,
 * and is available at https://www.eclipse.org/legal/epl-v10.html
 *
 * SPDX-License-Identifier: EPL-1.0
*/
#include "UTS_Strategy.h"
#include <cstring>
#include "Communication_Hash.h"

UTS_Strategy::UTS_Strategy(int initDepth, double den) {
    this->den = den;
    this->count = 0;
    this->size = 0;
    depth.resize(initDepth, 0);
    lower.resize(initDepth, 0);
    upper.resize(initDepth, 0);
    hash.resize(initDepth * 20 + 4, 0);

    SHA1_Init(&ctx);
}

UTS_Strategy::UTS_Strategy(double den) {
    this->den = den;
    this->count = 0;
    this->size = 0;
    depth.resize(64, 0);
    lower.resize(64, 0);
    upper.resize(64, 0);
    hash.resize(64 * 20 + 4, 0);

    SHA1_Init(&ctx);
}


void UTS_Strategy::digest(int d, SHA_CTX &ctx) {
    if (size >= depth.size()) {
        grow();
    }
    count++;

    const int offset = size * 20;

    SHA1_Final(hash.data() + offset, &ctx);

    const int v = ((0x7f & hash[offset + 16]) << 24)
                  | ((0xff & hash[offset + 17]) << 16)
                  | ((0xff & hash[offset + 18]) << 8)
                  | (0xff & hash[offset + 19]);

    const int n = (int) floor(log(1.0 - v / 2147483648.0) / den);

    if (n > 0) {
        if (d > 1) {
            depth[size] = d - 1;
            lower[size] = 0;
            upper[size] = n;
            size++;
        } else {
            count += n;
        }
    }
}

void UTS_Strategy::expand(SHA_CTX &ctx) {
    int top = size - 1;
    int d = depth[top];
    int l = lower[top];
    int u = upper[top] - 1;

    if (u == l) {
        size = top;
    } else {
        upper[top] = u;
    }

    int offset = top * 20;

    hash[offset + 20] = static_cast<unsigned char>(u >> 24);
    hash[offset + 21] = static_cast<unsigned char>(u >> 16);
    hash[offset + 22] = static_cast<unsigned char>(u >> 8);
    hash[offset + 23] = static_cast<unsigned char>(u);
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, hash.data() + offset, 24);

    digest(d, ctx);
}

long UTS_Strategy::getResult() {
    return count;
}

void UTS_Strategy::grow() {
    size_t newSize = depth.size() * 2;
    depth.resize(newSize, 0);
    lower.resize(newSize, 0);
    upper.resize(newSize, 0);
    hash.resize(newSize * 20 + 4, 0);
}

void UTS_Strategy::process(Communication_Hash &communication) {
    long n = 0;
    while (size > 0) {
        n++;
        expand(ctx);

        if (n > 511) {
            communication.sending++;

            if (communication.getRank() == 0) {
                communication.lookForTermination();
                communication.checkTime(*this);
            } else {
                communication.checkResourceChangeLocal(*this);
            }

            if (!communication.getTerminationFlag() && !communication.checkForRequest(*this)) {
                communication.checkIdleViaLifeLine();
            }
            n = 0;
        }
    }
}

void UTS_Strategy::merge(UTS_Strategy b) {
    while (size + b.size >= depth.size()) {
        grow();
    }
    std::copy(b.hash.begin(), b.hash.begin() + b.size * 20, hash.begin() + size * 20);
    std::copy(b.depth.begin(), b.depth.begin() + b.size, depth.begin() + size);
    std::copy(b.lower.begin(), b.lower.begin() + b.size, lower.begin() + size);
    std::copy(b.upper.begin(), b.upper.begin() + b.size, upper.begin() + size);

    size = size + b.size;
}

UTS_Strategy UTS_Strategy::split() {
    UTS_Strategy b = UTS_Strategy(den);
    for (int i = 0; i < size; ++i) {
        int p = upper[i] - lower[i];
        if (p >= 2) {
            if (b.size >= b.depth.size()) {
                b.grow();
            }

            std::copy(hash.begin() + i * 20, hash.begin() + i * 20 + 20, b.hash.begin() + b.size * 20);
            b.depth[b.size] = depth[i];
            b.upper[b.size] = upper[i];
            b.lower[b.size] = upper[i] -= p / 2;
            b.size++;
        }
    }
    return b;
}

UTS_Strategy UTS_Strategy::splitSub(int from, int to) {
    UTS_Strategy b = UTS_Strategy(den);
    for (int i = from; i < to; ++i) {
        if (b.size >= b.depth.size()) {
            b.grow();
        }

        std::copy(hash.begin() + i * 20, hash.begin() + i * 20 + 20, b.hash.begin() + b.size * 20);
        b.depth[b.size] = depth[i];
        b.upper[b.size] = upper[i];
        b.lower[b.size] = lower[i];
        b.size++;
    }
    return b;
}

void UTS_Strategy::clear() {
    depth.clear();
    lower.clear();
    upper.clear();
    hash.clear();
    size = 0;
}

void UTS_Strategy::addCount(long newCount) {
    count += newCount;
}

bool UTS_Strategy::checkIsSplittable() {
    int s = 0;
    for (int i = 0; i < size; ++i) {
        if ((upper[i] - lower[i]) >= 2) {
            ++s;
        }
    }
    return s > 0;
}

void UTS_Strategy::seed(int seed, int d) {
    std::fill(hash.begin(), hash.begin() + 16, 0);
    hash[16] = static_cast<unsigned char>(seed >> 24);
    hash[17] = static_cast<unsigned char>(seed >> 16);
    hash[18] = static_cast<unsigned char>(seed >> 8);
    hash[19] = static_cast<unsigned char>(seed);
    SHA1_Update(&ctx, hash.data(), 20);
    digest(d, ctx);
}
