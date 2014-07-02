#include <stdio.h>
#include <stdlib.h>
#include "mpiasp.h"

int MPI_Win_flush(int rank, MPI_Win win)
{
    MPIASP_Win *ua_win;
    int mpi_errno = MPI_SUCCESS;
    int user_rank, rank_in_local_ua = 0, rank_in_ua = 0;
    int is_shared = 0;
    int target_node_id = -1;

    MPIASP_DBG_PRINT_FCNAME();

    mpi_errno = get_ua_win(win, &ua_win);
    if (mpi_errno != MPI_SUCCESS)
        goto fn_fail;

    if (ua_win > 0) {
        PMPI_Comm_rank(ua_win->user_comm, &user_rank);

        mpi_errno = MPIASP_Is_in_shrd_mem(rank, ua_win->user_group, &target_node_id, &is_shared);
        if (mpi_errno != MPI_SUCCESS)
            goto fn_fail;

        /* If target is in shared memory, only flush target in local shared window. */
        if (is_shared) {
            PMPI_Group_translate_ranks(ua_win->user_group, 1, &rank,
                                       ua_win->local_ua_group, &rank_in_local_ua);

            MPIASP_DBG_PRINT("[%d]flush(%d, local_ua_win), instead of %d, node_id %d\n",
                             user_rank, rank_in_local_ua, rank, target_node_id);

            mpi_errno = PMPI_Win_flush(rank_in_local_ua, ua_win->local_ua_win);
            if (mpi_errno != MPI_SUCCESS)
                goto fn_fail;
        }
        else {
            MPIASP_DBG_PRINT("[%d]flush(Helper(%d), ua_wins[%d]), instead of %d, node_id %d\n",
                             user_rank, ua_win->asp_ranks_in_ua[target_node_id], rank,
                             rank, target_node_id);

            /* We only flush target helper in ua_window. Because for non-shared
             * targets, all translated operations are issued to target helper via ua_window.
             * Otherwise, they are issued through user window.
             */
            mpi_errno = PMPI_Win_flush(ua_win->asp_ranks_in_ua[target_node_id],
                                       ua_win->ua_wins[rank]);
            if (mpi_errno != MPI_SUCCESS)
                goto fn_fail;
        }

        // TODO: we have not implement translation for all operations yet.
        // So still some of them are pushed into user window
//      goto fn_exit;
    }

    mpi_errno = PMPI_Win_flush(rank, win);

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
