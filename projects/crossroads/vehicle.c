#include <stdio.h>

#include "threads/thread.h"
#include "threads/synch.h"
#include "projects/crossroads/vehicle.h"
#include "projects/crossroads/map.h"
#include "projects/crossroads/ats.h"

#define CROSSROADPOSTIONNUM 8

int crossroad_inclue = 0;
const int crossroad_postion[CROSSROADPOSTIONNUM][2] = {{2,2},{2,3},{2,4},{3,4},{4,4},{4,3},{4,2},{3,2}};

bool check_crossroad_inclue(int row, int col){
	for(int i = 0; i < CROSSROADPOSTIONNUM; i++){
		if(crossroad_postion[i][0] == row && crossroad_postion[i][1] == col) return true;
	}
	return false;
}

int thread_cnt_cur = 0;
int activate_thread = 0;
int moving_thread = 0;


/* path. A:0 B:1 C:2 D:3 */
const struct position vehicle_path[4][4][12] = {
	/* from A */ {
		/* to A */
		{{4,0},{4,1},{4,2},{4,3},{4,4},{3,4},{2,4},{2,3},{2,2},{2,1},{2,0},{-1,-1},},
		/* to B */
		{{4,0},{4,1},{4,2},{5,2},{6,2},{-1,-1},},
		/* to C */
		{{4,0},{4,1},{4,2},{4,3},{4,4},{4,5},{4,6},{-1,-1},},
		/* to D */
		{{4,0},{4,1},{4,2},{4,3},{4,4},{3,4},{2,4},{1,4},{0,4},{-1,-1},}
	},
	/* from B */ {
		/* to A */
		{{6,4},{5,4},{4,4},{3,4},{2,4},{2,3},{2,2},{2,1},{2,0},{-1,-1},},
		/* to B */
		{{6,4},{5,4},{4,4},{3,4},{2,4},{2,3},{2,2},{3,2},{4,2},{5,2},{6,2},{-1,-1},},
		/* to C */
		{{6,4},{5,4},{4,4},{4,5},{4,6},{-1,-1},},
		/* to D */
		{{6,4},{5,4},{4,4},{3,4},{2,4},{1,4},{0,4},{-1,-1},}
	},
	/* from C */ {
		/* to A */
		{{2,6},{2,5},{2,4},{2,3},{2,2},{2,1},{2,0},{-1,-1},},
		/* to B */
		{{2,6},{2,5},{2,4},{2,3},{2,2},{3,2},{4,2},{5,2},{6,2},{-1,-1},},
		/* to C */
		{{2,6},{2,5},{2,4},{2,3},{2,2},{3,2},{4,2},{4,3},{4,4},{4,5},{4,6},{-1,-1},},
		/* to D */
		{{2,6},{2,5},{2,4},{1,4},{0,4},{-1,-1},}
	},
	/* from D */ {
		/* to A */
		{{0,2},{1,2},{2,2},{2,1},{2,0},{-1,-1},},
		/* to B */
		{{0,2},{1,2},{2,2},{3,2},{4,2},{5,2},{6,2},{-1,-1},},
		/* to C */
		{{0,2},{1,2},{2,2},{3,2},{4,2},{4,3},{4,4},{4,5},{4,6},{-1,-1},},
		/* to D */
		{{0,2},{1,2},{2,2},{3,2},{4,2},{4,3},{4,4},{3,4},{2,4},{1,4},{0,4},{-1,-1},}
	}
};

static int is_position_outside(struct position pos)
{
	return (pos.row == -1 || pos.col == -1);
}

/* return 0:termination, 1:success, -1:fail, 2:standby */
static int try_move(int start, int dest, int step, struct vehicle_info *vi)
{
	struct position pos_cur, pos_next;

	pos_next = vehicle_path[start][dest][step];
	pos_cur = vi->position;

	if (vi->state == VEHICLE_STATUS_RUNNING) {
		/* check termination */
		if (is_position_outside(pos_next)) {
			/* actual move */
			vi->position.row = vi->position.col = -1;
			/* release previous */
			lock_release(&vi->map_locks[pos_cur.row][pos_cur.col]);
			return 0;
		}
	}

	if(!check_crossroad_inclue(pos_cur.row, pos_cur.col) && check_crossroad_inclue(pos_next.row, pos_next.col)){
		if(crossroad_inclue >= 7){
			if (vi->state == VEHICLE_STATUS_READY) {
				vi->state = VEHICLE_STATUS_RUNNING;
			} else {
				lock_release(&vi->map_locks[pos_cur.row][pos_cur.col]);
			}
			lock_acquire(&vi->map_locks[pos_cur.row][pos_cur.col]);
			vi->position = pos_cur;

			return 2;
		}else{
			crossroad_inclue++;
			lock_acquire(&vi->map_locks[pos_next.row][pos_next.col]);
			if (vi->state == VEHICLE_STATUS_READY) {
				/* start this vehicle */
				vi->state = VEHICLE_STATUS_RUNNING;
			} else {
				/* release current position */
				lock_release(&vi->map_locks[pos_cur.row][pos_cur.col]);
			}
			/* update position */
			vi->position = pos_next;
			return 1;
		}
	}else if(check_crossroad_inclue(pos_cur.row, pos_cur.col) && !check_crossroad_inclue(pos_next.row, pos_next.col)){
		crossroad_inclue--;
		lock_acquire(&vi->map_locks[pos_next.row][pos_next.col]);
		if (vi->state == VEHICLE_STATUS_READY) {
			/* start this vehicle */
			vi->state = VEHICLE_STATUS_RUNNING;
		} else {
			/* release current position */
			lock_release(&vi->map_locks[pos_cur.row][pos_cur.col]);
		}
		/* update position */
		vi->position = pos_next;
		return 1;
	}else{
		lock_acquire(&vi->map_locks[pos_next.row][pos_next.col]);
		if (vi->state == VEHICLE_STATUS_READY) {
			/* start this vehicle */
			vi->state = VEHICLE_STATUS_RUNNING;
		} else {
			/* release current position */
			lock_release(&vi->map_locks[pos_cur.row][pos_cur.col]);
		}
		/* update position */
		vi->position = pos_next;
		return 1;
	}
}

void init_on_mainthread(int thread_cnt){
	/* Called once before spawning threads */
	thread_cnt_cur = thread_cnt;
}

void vehicle_loop(void *_vi)
{
	int res;
	int start, dest, step;

	struct vehicle_info *vi = _vi;

	start = vi->start - 'A';
	dest = vi->dest - 'A';

	vi->position.row = vi->position.col = -1;
	vi->state = VEHICLE_STATUS_READY;

	step = 0;
	while (1) {
		/* vehicle main code */
		if(step != 0) thread_cnt_cur--;

		res = try_move(start, dest, step, vi);

		if(step != 0) thread_cnt_cur++;
		if(step == 0) activate_thread++;

		if (res == 1) {
			step++;
		}

		/* termination condition. */ 
		if (res == 0) {
			thread_cnt_cur--;
			break;
		}

		if(activate_thread >= thread_cnt_cur) moving_thread++;

		/* unitstep change! */
		if(moving_thread >= thread_cnt_cur) {
			crossroads_step++;
			moving_thread = 0;
		}

		unitstep_changed();
	}	

	/* status transition must happen before sema_up */
	vi->state = VEHICLE_STATUS_FINISHED;
}
