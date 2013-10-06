/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 et
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozStorageStatementData_h
#define mozStorageStatementData_h

#include "sqlite3.h"

#include "nsAutoPtr.h"
#include "nsTArray.h"
#include "nsIEventTarget.h"
#include "mozilla/Util.h"
#include "nsThreadUtils.h"

#include "mozStorageBindingParamsArray.h"
#include "mozIStorageBaseStatement.h"
#include "mozStorageConnection.h"
#include "StorageBaseStatementInternal.h"

struct sqlite3_stmt;

namespace mozilla {
namespace storage {

class StatementData
{
public:
  StatementData(sqlite3_stmt *aStatement,
                already_AddRefed<BindingParamsArray> aParamsArray,
                StorageBaseStatementInternal *aStatementOwner)
  : mStatement(aStatement)
  , mParamsArray(aParamsArray)
  , mStatementOwner(aStatementOwner)
  {
    NS_PRECONDITION(mStatementOwner, "Must have a statement owner!");
  }
  StatementData(const StatementData &aSource)
  : mStatement(aSource.mStatement)
  , mParamsArray(aSource.mParamsArray)
  , mStatementOwner(aSource.mStatementOwner)
  {
    NS_PRECONDITION(mStatementOwner, "Must have a statement owner!");
  }
  StatementData()
  {
  }

  /**
   * Return the sqlite statement, fetching it from the storage statement.  In
   * the case of AsyncStatements this may actually create the statement 
   */
  inline int getSqliteStatement(sqlite3_stmt **_stmt)
  {
    if (!mStatement) {
      int rc = mStatementOwner->getAsyncStatement(&mStatement);
      NS_ENSURE_TRUE(rc == SQLITE_OK, rc);
    }
    *_stmt = mStatement;
    return SQLITE_OK;
  }

  operator BindingParamsArray *() const { return mParamsArray; }

  /**
   * Provide the ability to coerce back to a sqlite3 * connection for purposes 
   * of getting an error message out of it.
   */
  operator sqlite3 *() const
  {
    return mStatementOwner->getOwner()->GetNativeConnection();
  }

  /**
   * NULLs out our sqlite3_stmt (it is held by the owner) after reseting it and
   * clear all bindings to it.  This is expected to occur on the async thread.
   */
  inline void finalize()
  {
    NS_PRECONDITION(mStatementOwner, "Must have a statement owner!");
#ifdef DEBUG
    {
      nsCOMPtr<nsIEventTarget> asyncThread =
        mStatementOwner->getOwner()->getAsyncExecutionTarget();
      // It's possible that we are shutting down the async thread, and this
      // method would return nullptr as a result.
      if (asyncThread) {
        bool onAsyncThread;
        NS_ASSERTION(NS_SUCCEEDED(asyncThread->IsOnCurrentThread(&onAsyncThread)) && onAsyncThread,
                     "This should only be running on the async thread!");
      }
    }
#endif
    // In the AsyncStatement case we may never have populated mStatement if the
    // AsyncExecuteStatements got canceled or a failure occurred in constructing
    // the statement.
    if (mStatement) {
      (void)::sqlite3_reset(mStatement);
      (void)::sqlite3_clear_bindings(mStatement);
      mStatement = nullptr;
    }
  }

  /**
   * Indicates if this statement has parameters to be bound before it is
   * executed.
   *
   * @return true if the statement has parameters to bind against, false
   *         otherwise.
   */
  inline bool hasParametersToBeBound() const { return !!mParamsArray; }
  /**
   * Indicates the number of implicit statements generated by this statement
   * requiring a transaction for execution.  For example a single statement
   * with N BindingParams will execute N implicit staments.
   *
   * @return number of statements requiring a transaction for execution.
   *
   * @note In the case of AsyncStatements this may actually create the
   *       statement.
   */
  inline uint32_t needsTransaction()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    // Be sure to use the getSqliteStatement helper, since sqlite3_stmt_readonly
    // can only analyze prepared statements and AsyncStatements are prepared
    // lazily.
    sqlite3_stmt *stmt;
    int rc = getSqliteStatement(&stmt);
    if (SQLITE_OK != rc || ::sqlite3_stmt_readonly(stmt)) {
      return 0;
    }
    return mParamsArray ? mParamsArray->length() : 1;
  }

private:
  sqlite3_stmt *mStatement;
  nsRefPtr<BindingParamsArray> mParamsArray;

  /**
   * We hold onto a reference of the statement's owner so it doesn't get
   * destroyed out from under us.
   */
  nsCOMPtr<StorageBaseStatementInternal> mStatementOwner;
};

} // namespace storage
} // namespace mozilla

#endif // mozStorageStatementData_h
