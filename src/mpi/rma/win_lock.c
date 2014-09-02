#include <stdio.h>
#include <stdlib.h>
#include "mtcore.h"

int MPI_Win_lock(int lock_type, int target_rank, int assert, MPI_Win win)
{
    MTCORE_Win *uh_win;
    int mpi_errno = MPI_SUCCESS;
    int user_rank;
    int j, k;

    MTCORE_DBG_PRINT_FCNAME();

    MTCORE_Fetch_uh_win_from_cache(win, uh_win);

    PMPI_Comm_rank(uh_win->user_comm, &user_rank);

    uh_win->targets[target_rank].remote_lock_assert = assert;
    MTCORE_DBG_PRINT("[%d]lock(%d), MPI_MODE_NOCHECK %d(assert %d)\n", user_rank,
                     target_rank, (assert & MPI_MODE_NOCHECK) != 0, assert);

    /* Lock Helper processes in corresponding uh-window of target process. */
    for (j = 0; j < uh_win->targets[target_rank].num_segs; j++) {
#ifdef MTCORE_ENABLE_SYNC_ALL_OPT

        /* Optimization for MPI implementations that have optimized lock_all.
         * However, user should be noted that, if MPI implementation issues lock messages
         * for every target even if it does not have any operation, this optimization
         * could lose performance and even lose asynchronous! */

        MTCORE_DBG_PRINT("[%d]lock_all(uh_wins 0x%x), instead of target rank %d seg %d\n",
                         user_rank, uh_win->targets[target_rank].segs[j].uh_win, target_rank, j);
        mpi_errno = PMPI_Win_lock_all(assert, uh_win->targets[target_rank].segs[j].uh_win);
        if (mpi_errno != MPI_SUCCESS)
            goto fn_fail;
#else
        for (k = 0; k < MTCORE_NUM_H; k++) {
            int target_h_rank_in_uh = uh_win->targets[target_rank].h_ranks_in_uh[k];

            MTCORE_DBG_PRINT("[%d]lock(Helper(%d), uh_wins 0x%x), instead of "
                             "target rank %d seg %d\n", user_rank, target_h_rank_in_uh,
                             uh_win->targets[target_rank].segs[j].uh_win, target_rank, j);

            mpi_errno = PMPI_Win_lock(lock_type, target_h_rank_in_uh, assert,
                                      uh_win->targets[target_rank].segs[j].uh_win);
            if (mpi_errno != MPI_SUCCESS)
                goto fn_fail;
        }
#endif
    }

#ifdef MTCORE_ENABLE_LOCAL_LOCK_OPT
    uh_win->is_self_locked = 0;
#endif

    if (user_rank == target_rank) {
        int is_local_lock_granted = 0;

        /* If target is itself, we need grant this lock before return.
         * However, the actual locked processes are the Helpers whose locks may be delayed by
         * most MPI implementation, thus we need a flush to force the lock to be granted.
         *
         * For performance reason, this operation is ignored if meet at least one of following conditions:
         * 1. if user passed information that this process will not do local load/store on this window.
         * 2. if user passed information that there is no concurrent epochs.
         */
        if (!uh_win->info_args.no_local_load_store &&
            !(uh_win->targets[target_rank].remote_lock_assert & MPI_MODE_NOCHECK)) {
            for (j = 0; j < uh_win->targets[target_rank].num_segs; j++) {
                mpi_errno = MTCORE_Win_grant_local_lock(user_rank, j, lock_type, assert, uh_win);
                if (mpi_errno != MPI_SUCCESS)
                    goto fn_fail;
            }
            is_local_lock_granted = 1;
        }

#ifdef MTCORE_ENABLE_LOCAL_LOCK_OPT
        if (is_local_lock_granted ||
            (uh_win->targets[target_rank].remote_lock_assert & MPI_MODE_NOCHECK)) {
            /* Lock local rank so that operations can be executed through local target.
             * 1. Need grant lock on helper in advance due to permission check,
             * OR
             * 2. there is no concurrent epochs, hence it is safe to get local lock.*/
            int local_uh_rank;
            PMPI_Comm_rank(uh_win->local_uh_comm, &local_uh_rank);

            MTCORE_DBG_PRINT("[%d]lock self(%d, local_uh_win)\n", user_rank, local_uh_rank);
            mpi_errno = PMPI_Win_lock(lock_type, local_uh_rank, assert, uh_win->local_uh_win);
            if (mpi_errno != MPI_SUCCESS)
                goto fn_fail;
            uh_win->is_self_locked = 1;
        }
#endif
    }

#if (MTCORE_LOAD_OPT != MTCORE_LOAD_OPT_NON)
    for (j = 0; j < uh_win->targets[target_rank].num_segs; j++) {
        uh_win->targets[target_rank].segs[j].main_lock_stat = MTCORE_MAIN_LOCK_RESET;

        MTCORE_Reset_win_target_ordering(target_rank, j, uh_win);
        MTCORE_Reset_win_target_load_opt(target_rank, uh_win);
    }
#endif

    /* TODO: All the operations which we have not wrapped up will be failed, because they
     * are issued to user window. We need wrap up all operations.
     */

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
