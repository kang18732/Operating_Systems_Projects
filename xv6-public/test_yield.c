#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
	int pid;
	for(int i = 0; i < 3; i++) {
		if((pid = fork()) < 0) {
			printf(1, "Error\n");
		}
		else if(pid == 0) {
			printf(1, "Child\n");
		}
		else {
			printf(1, "Parent\n");
		}
		yield();
	}
	exit();
}
