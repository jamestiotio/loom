/*
 * Copyright (c) 2022, Red Hat, Inc. All rights reserved.
 * Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "memory/allocation.hpp"
#include "oops/markWord.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/lockStack.inline.hpp"
#include "runtime/objectMonitor.inline.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/stackWatermark.hpp"
#include "runtime/stackWatermarkSet.inline.hpp"
#include "runtime/thread.hpp"
#include "utilities/copy.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/ostream.hpp"

#include <type_traits>

const int LockStack::lock_stack_offset =      in_bytes(JavaThread::lock_stack_offset());
const int LockStack::lock_stack_top_offset =  in_bytes(JavaThread::lock_stack_top_offset());
const int LockStack::lock_stack_base_offset = in_bytes(JavaThread::lock_stack_base_offset());

LockStack::LockStack(JavaThread* jt) :
  _top(lock_stack_base_offset), _base() {
  // Make sure the layout of the object is compatible with the emitted code's assumptions.
  STATIC_ASSERT(sizeof(_bad_oop_sentinel) == oopSize);
  STATIC_ASSERT(sizeof(_base[0]) == oopSize);
  STATIC_ASSERT(std::is_standard_layout<LockStack>::value);
  STATIC_ASSERT(offsetof(LockStack, _bad_oop_sentinel) == offsetof(LockStack, _base) - oopSize);
#ifdef ASSERT
  for (int i = 0; i < CAPACITY; i++) {
    _base[i] = nullptr;
  }
#endif
}

uint32_t LockStack::start_offset() {
  int offset = lock_stack_base_offset;
  assert(offset > 0, "must be positive offset");
  return static_cast<uint32_t>(offset);
}

uint32_t LockStack::end_offset() {
  int offset = lock_stack_base_offset + CAPACITY * oopSize;
  assert(offset > 0, "must be positive offset");
  return static_cast<uint32_t>(offset);
}

#ifndef PRODUCT
void LockStack::verify(const char* msg) const {
  assert(LockingMode == LM_LIGHTWEIGHT, "never use lock-stack when light weight locking is disabled");
  assert((_top <= end_offset()), "lockstack overflow: _top %d end_offset %d", _top, end_offset());
  assert((_top >= start_offset()), "lockstack underflow: _top %d start_offset %d", _top, start_offset());
  if (SafepointSynchronize::is_at_safepoint() || (Thread::current()->is_Java_thread() && is_owning_thread())) {
    int top = to_index(_top);
    for (int i = 0; i < top; i++) {
      assert(_base[i] != nullptr, "no zapped before top");
      if (VM_Version::supports_recursive_lightweight_locking()) {
        oop o = _base[i];
        for (; i < top - 1; i++) {
          // Consecutive entries may be the same
          if (_base[i + 1] != o) {
            break;
          }
        }
      }

      for (int j = i + 1; j < top; j++) {
        assert(_base[i] != _base[j], "entries must be unique: %s", msg);
      }
    }
    for (int i = top; i < CAPACITY; i++) {
      assert(_base[i] == nullptr, "only zapped entries after top: i: %d, top: %d, entry: " PTR_FORMAT, i, top, p2i(_base[i]));
    }
  }
}
#endif

#ifdef ASSERT
void LockStack::verify_consistent_lock_order(GrowableArray<oop>& lock_order, bool leaf_frame) const {
  int top_index = to_index(_top);
  int lock_index = lock_order.length();

  if (!leaf_frame) {
    // If the lock_order is not from the leaf frame we must search
    // for the top_index which fits with the most recent fast_locked
    // objects in the lock stack.
    while (lock_index-- > 0) {
      const oop obj = lock_order.at(lock_index);
      if (contains(obj)) {
        for (int index = 0; index < top_index; index++) {
          if (_base[index] == obj) {
            // Found top index
            top_index = index + 1;
            break;
          }
        }

        if (VM_Version::supports_recursive_lightweight_locking()) {
          // With recursive looks there may be more of the same object
          while (lock_index-- > 0 && lock_order.at(lock_index) == obj) {
            top_index++;
          }
          assert(top_index <= to_index(_top), "too many obj in lock_order");
        }

        break;
      }
    }

    lock_index = lock_order.length();
  }

  while (lock_index-- > 0) {
    const oop obj = lock_order.at(lock_index);
    const markWord mark = obj->mark_acquire();
    assert(obj->is_locked(), "must be locked");
    if (top_index > 0 && obj == _base[top_index - 1]) {
      assert(mark.is_fast_locked() || mark.monitor()->is_owner_anonymous(),
             "must be fast_locked or inflated by other thread");
      top_index--;
    } else {
      assert(!mark.is_fast_locked(), "must be inflated");
      assert(mark.monitor()->is_owner(get_thread()) ||
             (!leaf_frame && get_thread()->current_waiting_monitor() == mark.monitor()),
             "must be owned by (or waited on by) thread");
      assert(!contains(obj), "must not be on lock_stack");
    }
  }
}
#endif

void LockStack::print_on(outputStream* st) {
  for (int i = to_index(_top); (--i) >= 0;) {
    st->print("LockStack[%d]: ", i);
    oop o = _base[i];
    if (oopDesc::is_oop(o)) {
      o->print_on(st);
    } else {
      st->print_cr("not an oop: " PTR_FORMAT, p2i(o));
    }
  }
}
