// Copyright 2014 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "util/misc/initialization_state_dcheck.h"

#include "base/logging.h"
#include "gtest/gtest.h"
#include "util/test/gtest_death_check.h"

namespace crashpad {
namespace test {
namespace {

TEST(InitializationStateDcheck, InitializationStateDcheck) {
  InitializationStateDcheck initialization_state_dcheck;
  INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
  INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck);
  INITIALIZATION_STATE_DCHECK_VALID(initialization_state_dcheck);
}

#if DCHECK_IS_ON

// InitializationStateDcheck only DCHECKs, so the death tests can only run
// when DCHECKs are enabled.

TEST(InitializationStateDcheckDeathTest, Uninitialized_NotInvalid) {
  // This tests that an attempt to set an uninitialized object as valid without
  // transitioning through the initializing (invalid) state fails.
  InitializationStateDcheck initialization_state_dcheck;
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck),
      "kStateInvalid");
}

TEST(InitializationStateDcheckDeathTest, Uninitialized_NotValid) {
  // This tests that an attempt to use an uninitialized object as though it
  // were valid fails.
  InitializationStateDcheck initialization_state_dcheck;
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_DCHECK_VALID(initialization_state_dcheck),
      "kStateValid");
}

TEST(InitializationStateDcheckDeathTest, Invalid_NotUninitialized) {
  // This tests that an attempt to begin initializing an object on which
  // initialization was already attempted fails.
  InitializationStateDcheck initialization_state_dcheck;
  INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck),
      "kStateUninitialized");
}

TEST(InitializationStateDcheckDeathTest, Invalid_NotValid) {
  // This tests that an attempt to use an initializing object as though it
  // were valid fails.
  InitializationStateDcheck initialization_state_dcheck;
  INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_DCHECK_VALID(initialization_state_dcheck),
      "kStateValid");
}

TEST(InitializationStateDcheckDeathTest, Valid_NotUninitialized) {
  // This tests that an attempt to begin initializing an object that has already
  // been initialized fails.
  InitializationStateDcheck initialization_state_dcheck;
  INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
  INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck);
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck),
      "kStateUninitialized");
}

TEST(InitializationStateDcheckDeathTest, Valid_NotInvalid) {
  // This tests that an attempt to set a valid object as valid a second time
  // fails.
  InitializationStateDcheck initialization_state_dcheck;
  INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
  INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck);
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck),
      "kStateInvalid");
}

TEST(InitializationStateDcheckDeathTest, Destroyed_NotUninitialized) {
  // This tests that an attempt to reinitialize a destroyed object fails. See
  // the InitializationState.InitializationState test for an explanation of this
  // use-after-free test.
  InitializationStateDcheck* initialization_state_dcheck_pointer;
  {
    InitializationStateDcheck initialization_state_dcheck;
    initialization_state_dcheck_pointer = &initialization_state_dcheck;
    INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
    INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck);
    INITIALIZATION_STATE_DCHECK_VALID(initialization_state_dcheck);
  }
  ASSERT_DEATH_CHECK(INITIALIZATION_STATE_SET_INITIALIZING(
                         *initialization_state_dcheck_pointer),
                     "kStateUninitialized");
}

TEST(InitializationStateDcheckDeathTest, Destroyed_NotValid) {
  // This tests that an attempt to use a destroyed object fails. See the
  // InitializationState.InitializationState test for an explanation of this
  // use-after-free test.
  InitializationStateDcheck* initialization_state_dcheck_pointer;
  {
    InitializationStateDcheck initialization_state_dcheck;
    initialization_state_dcheck_pointer = &initialization_state_dcheck;
    INITIALIZATION_STATE_SET_INITIALIZING(initialization_state_dcheck);
    INITIALIZATION_STATE_SET_VALID(initialization_state_dcheck);
    INITIALIZATION_STATE_DCHECK_VALID(initialization_state_dcheck);
  }
  ASSERT_DEATH_CHECK(
      INITIALIZATION_STATE_DCHECK_VALID(*initialization_state_dcheck_pointer),
      "kStateValid");
}

#endif

}  // namespace
}  // namespace test
}  // namespace crashpad
