#include "thread.h"

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60;	//单位：秒
int Thread::m_generateId = 0;

/*
=========Thread实现=============
*/
Thread::Thread(ThreadFunc func)
	:m_func(func)
	, m_threadId(m_generateId++)
{
}

void Thread::start()
{
	//创建一个线程来执行一个线程函数
	std::thread t(m_func, m_threadId);
	t.detach();	//设置守护线程
}

int Thread::getId()const
{
	return m_threadId;
}


/*
========ThreadPool实现==========
*/
ThreadPool::ThreadPool()
	:m_initThreadSize(0)
	, m_taskSize(0)
	, m_idleThreadSize(0)
	, m_curThreadSize(0)
	, m_taskqueMaxThresHold(TASK_MAX_THRESHHOLD)
	, m_threadSizeThreshHold(THREAD_MAX_THRESHHOLD)
	, m_poolMode(PoolMode::MODE_FIXED)
	, m_isPoolRunning(false)
{
}

ThreadPool::~ThreadPool()
{
	m_isPoolRunning = false;

	//等待线程池里面所有的线程返回 线程有两种状态 阻塞和正在执行中
	std::unique_lock<std::mutex> lock(m_taskQueMtx);
	m_notEmpty.notify_all();
	m_exitCond.wait(lock, [&]()->bool {return m_threads.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())return;
	m_poolMode = mode;
}

void ThreadPool::setTaskQueMaxThrshHold(int threshhold)
{
	if (checkRunningState()) return;
	m_taskqueMaxThresHold = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshHold)
{
	if (checkRunningState()) return;
	if (m_poolMode == PoolMode::MODE_CACHED)
		m_threadSizeThreshHold = threshHold;
}

void ThreadPool::threadFunc(int threadId)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//所有任务必须执行完成，线程池才能回收所有线程资源
	while (true)
	{
		Task task;
		{
			//获取锁
			std::unique_lock<std::mutex> lock(m_taskQueMtx);
			//std::cout << "tid:" << std::this_thread::get_id() << "正在尝试获取任务" << std::endl;
			// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，应该把多余的线程
			// 结束回收掉（超过initThreadSize_数量的线程要进行回收）
			// 当前时间 - 上一次线程执行的时间 > 60s

			// 每一秒中返回一次   怎么区分：超时返回？还是有任务待执行返回
			// 锁 + 双重判断
			while (m_taskque.size() == 0)
			{
				//线程池要结束 回收线程资源
				if (!m_isPoolRunning)
				{
					m_threads.erase(threadId);
					std::cout << "thread id: " << std::this_thread::get_id << " exit" << std::endl;
					m_exitCond.notify_all();
					return;//线程函数结束 
				}

				if (m_poolMode == PoolMode::MODE_CACHED)
				{
					if (std::cv_status::timeout ==
						m_notEmpty.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& m_curThreadSize > m_initThreadSize)
						{
							// 开始回收当前线程
							// 记录线程数量的相关变量的值修改
							// 把线程对象从线程列表容器中删除   没有办法 threadFunc《=》thread对象
							// threadid => thread对象 => 删除
							m_threads.erase(threadId); // std::this_thread::getid()
							m_curThreadSize--;
							m_idleThreadSize--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else
				{
					//等待empty条件
					m_notEmpty.wait(lock);
				}
			}
			m_idleThreadSize--;
			//std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功" << std::endl;

			//从任务队列中取出任务
			task = m_taskque.front();
			m_taskque.pop();
			m_taskSize--;

			//如果仍然有任务 则通知其他线程
			if (m_taskque.size() > 0)
			{
				m_notEmpty.notify_all();
			}
			//取出一个任务进行通知 通知可以继续提交任务
			m_notFull.notify_all();
		}//把锁放掉

		//当前线程负责执行这个任务
		if (task != nullptr)
		{
			task();
		}
		m_idleThreadSize++;
		lastTime = std::chrono::high_resolution_clock().now();
	}
}

void ThreadPool::start(int initThreadSize)
{
	//设置线程池运行状态
	m_isPoolRunning = true;

	//记录初始线程池的个数
	m_curThreadSize = initThreadSize;
	m_initThreadSize = initThreadSize;

	//创建线程对象
	for (int i = 0; i < m_initThreadSize; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int	threadId = ptr->getId();
		m_threads.emplace(threadId, std::move(ptr));
	}

	for (int i = 0; i < m_initThreadSize; i++)
	{
		m_threads[i]->start();
		m_idleThreadSize++;
	}
}


bool ThreadPool::checkRunningState()const
{
	return m_isPoolRunning;
}