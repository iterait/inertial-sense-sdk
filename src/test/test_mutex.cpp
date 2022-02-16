#include <gtest/gtest.h>
#include <stdlib.h>
#include <cmath>
#include "../ISUtilities.h"
#include <thread>

TEST(IS_Mutex, locking_unlocking_manual)
{
	cMutex m;

	m.Lock();
	m.Unlock();
}


TEST(IS_Mutex, locking_unlocking_scoped)
{
	cMutex m;

	{
		cMutexLocker lock1(&m);
	}
	{
		cMutexLocker lock2(&m);
	}
}


TEST(IS_Mutex, locking_unlocking_multiple)
{
	cMutex m1;
	cMutex m2;

	cMutexLocker lock1(&m1);
	cMutexLocker lock2(&m2);
}

cMutex global_test_mutex;
 
void lock_and_unlock(int i)
{
	std::cout << "thread " << i << std::endl;
	cMutexLocker lock1(&global_test_mutex);
}

TEST(IS_Mutex, locking_unlocking_shared)
{
	std::thread t1(lock_and_unlock, 1);
	std::thread t2(lock_and_unlock, 2);
	std::thread t3(lock_and_unlock, 3);
	t1.join();
	t2.join();
	t3.join();
}
