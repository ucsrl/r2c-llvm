//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an abstraction for deterministic random number
// generation (RNG).  Note that the current implementation is not
// cryptographically secure as it uses the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h" // Needed for uint64_t on Windows.
#include <random>
#include <system_error>

namespace llvm {

/// A random number generator.
///
/// Instances of this class should not be shared across threads. The
/// seed should be set by passing the -rng-seed=<uint64> option. Use
/// Module::createRNG to create a new RNG instance for use with that
/// module.
class RandomNumberGenerator {

  // 64-bit Mersenne Twister by Matsumoto and Nishimura, 2000
  // http://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
  // This RNG is deterministically portable across C++11
  // implementations.
  using generator_type = std::mt19937_64;

public:
  using result_type = generator_type::result_type;

  /// Returns a random number in the range [0, Max).
  result_type operator()();

  static constexpr result_type min() { return generator_type::min(); }
  static constexpr result_type max() { return generator_type::max(); }

  uint64_t Random(uint64_t Max);

  /**
   * Shuffles an *array* of type T.
   *
   * Uses the Durstenfeld version of the Fisher-Yates method (aka the Knuth
   * method).  See http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
   */
  template<typename T>
  void shuffle(T* array, size_t length) {
    if (length == 0) return;
    for (size_t i = length - 1; i > 0; i--) {
      size_t j = Random(i + 1);
      if (j < i)
        std::swap(array[j], array[i]);
    }
  }


  /**
   * Shuffles a SmallVector of type T, default size N
   */
  template <typename T, unsigned N> void shuffle(SmallVector<T, N> &SV) {
    if (SV.empty())
      return;
    for (size_t I = SV.size() - 1; I > 0; I--) {
      size_t J = Random(I + 1);
      if (J < I)
        std::swap(SV[J], SV[I]);
    }
  }

  /**
   * Shuffles an SymbolTableList of type T
   */
  template <typename T> void shuffle(SymbolTableList<T> &Listt) {
    if (Listt.empty())
      return;
    SmallVector<T *, 10> SV;
    for (typename SymbolTableList<T>::iterator I = Listt.begin();
         I != Listt.end();) {
      /* iplist<T>::remove increments the iterator which is why the loop
       * doesn't.
       */
      T *Element = Listt.remove(I);
      SV.push_back(Element);
    }
    shuffle<T *, 10>(SV);
    for (typename SmallVector<T *, 10>::size_type I = 0; I < SV.size(); I++) {
      Listt.push_back(SV[I]);
    }
  }

private:
  /// Seeds and salts the underlying RNG engine.
  ///
  /// This constructor should not be used directly. Instead use
  /// Module::createRNG to create a new RNG salted with the Module ID.
  RandomNumberGenerator(StringRef Salt);

  generator_type Generator;

  // Noncopyable.
  RandomNumberGenerator(const RandomNumberGenerator &Other) = delete;
  RandomNumberGenerator &operator=(const RandomNumberGenerator &Other) = delete;

  friend class Module;
};

// Get random vector of specified size
std::error_code getRandomBytes(void *Buffer, size_t Size);

} // namespace llvm

#endif