#include <stdio.h>
#include <stdlib.h>
#include "mpiasp.h"

int MPI_Win_unlock(int target_rank, MPI_Win win)
{
    MPIASP_Win *ua_win;
    int mpi_errno = MPI_SUCCESS;
    int user_rank, rank_in_local_ua = 0;
    int target_node_id = -1;
    int target_local_rank = -1;

    MPIASP_DBG_PRINT_FCNAME();

    mpi_errno = get_ua_win(win, &ua_win);
    if (mpi_errno != MPI_SUCCESS)
        goto fn_fail;

    if (ua_win > 0) {
        target_local_rank = ua_win->local_user_ranks[target_rank];
        PMPI_Comm_rank(ua_win->user_comm, &user_rank);

        mpi_errno = MPIASP_Get_node_ids(ua_win->user_group, 1, &target_rank, &target_node_id);
        if (mpi_errno != MPI_SUCCESS)
            return mpi_errno;

        /* Unlock helper process in corresponding ua-window of target process. */
        MPIASP_DBG_PRINT("[%d]unlock(Helper(%d), ua_wins[%d]), instead of %d, node_id %d\n",
                         user_rank, ua_win->asp_ranks_in_ua[target_node_id], target_local_rank,
                         target_rank, target_node_id);

        mpi_errno = PMPI_Win_unlock(ua_win->asp_ranks_in_ua[target_node_id],
                                    ua_win->ua_wins[target_local_rank]);
        if (mpi_errno != MPI_SUCCESS)
            goto fn_fail;

#ifdef MTCORE_ENABLE_LOCAL_LOCK_OPT
        /* If target is itself, we need also release the lock of local rank  */
        if (user_rank == target_rank && ua_win->is_self_locked) {
            int local_ua_rank;
            PMPI_Comm_rank(ua_win->local_ua_comm, &local_ua_rank);

            MPIASP_DBG_PRINT("[%d]unlock self(%d, local_ua_win)\n", user_rank, local_ua_rank);
            mpi_errno = PMPI_Win_unlock(local_ua_rank, ua_win->local_ua_win);
            if (mpi_errno != MPI_SUCCESS)
                goto fn_fail;

            ua_win->is_self_locked = 0;
        }
#endif

    }
    /* TODO: All the operations which we have not wrapped up will be failed, because they
     * are issued to user window. We need wrap up all operations.
     */
    else {
        mpi_errno = PMPI_Win_unlock(target_rank, win);
    }

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
