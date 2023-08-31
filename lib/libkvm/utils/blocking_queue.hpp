#include <vector>
#include <mutex>

namespace kvm {

template <typename T>
class BlockingQueue {
public:
	void push(const T& item)
	{
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_queue.push_back(item);
		}
		m_cv.notify_one();
	}

	bool pop(T& item)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		for (;;) {
			if (m_queue.empty()) {
				if (m_shutdown) {
					return false;
				}
			}
			else break;
			m_cv.wait(lock);
		}
		item = std::move(m_queue.back());
		m_queue.pop_back();
		return true;
	}

	BlockingQueue() = default;
	BlockingQueue(std::vector<T>&& q)
		: m_queue{std::move(q)} {}
	~BlockingQueue() { shutdown(); }

private:
	void shutdown()
	{
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_shutdown = true;
		}
		m_cv.notify_all();
	}

	std::condition_variable m_cv;
	std::mutex m_mutex;
	std::vector<T> m_queue;
	bool m_shutdown = false;
};

} // kvm
