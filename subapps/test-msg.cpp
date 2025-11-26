/*
		while (1) {
			break;
			sysrest();
			stduint now = syssecond();
			if (now == 2 && stage == 0) {
				//syssend(Task_AppB, &msg);// sync
				stage = 1;
			}
			if (now == 3 && stage == 1) {
				tmp = 0xABCD;
				//syssend(Task_AppB, &msg);// sync
			}
		}
		// {
		// 	struct CommMsg msg0 = { 0 };
		// 	stduint tmp;
		// 	msg0.address = _IMM(&tmp); msg0.length = sizeof(tmp);
		// 	msg0.type = 0;
		// 	syssend(Task_FileSys, &msg);
		// }
*/