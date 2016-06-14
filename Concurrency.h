#pragma once

#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>
namespace conc {

	using namespace std;
	class thread_pool {
	public:
		thread_pool() = default;
		thread_pool(int nthreads) : _system(nthreads) {}
		/**************************************************************************************************/


		/**************************************************************************************************/

		using lock_t = std::unique_lock<mutex>;

		class notification_queue {
			deque<function<void()>> _q;
			bool                    _done{ false };
			mutex                   _mutex;
			condition_variable      _ready;

		public:
			bool try_pop(function<void()>& x) {
				lock_t lock{ _mutex, try_to_lock };
				if (!lock || _q.empty()) return false;
				x = move(_q.front());
				_q.pop_front();
				return true;
			}

			template<typename F>
			bool try_push(F&& f) {
				{
					lock_t lock{ _mutex, try_to_lock };
					if (!lock) return false;
					_q.emplace_back(forward<F>(f));
				}
				_ready.notify_one();
				return true;
			}

			void done() {
				{
					unique_lock<mutex> lock{ _mutex };
					_done = true;
				}
				_ready.notify_all();
			}

			bool pop(function<void()>& x) {
				lock_t lock{ _mutex };
				while (_q.empty() && !_done) _ready.wait(lock);
				if (_q.empty()) return false;
				x = move(_q.front());
				_q.pop_front();
				return true;
			}

			template<typename F>
			void push(F&& f) {
				{
					lock_t lock{ _mutex };
					_q.emplace_back(forward<F>(f));
				}
				_ready.notify_one();
			}
		};

		/**************************************************************************************************/

		class task_system {
			const unsigned              _count{  };
			vector<thread>              _threads;
			vector<notification_queue>  _q{ _count };
			atomic<unsigned>            _index{ 0 };

			void run(unsigned i) {
				while (true) {
					function<void()> f;

					for (unsigned n = 0; n != _count * 32; ++n) {
						if (_q[(i + n) % _count].try_pop(f)) break;
					}
					if (!f && !_q[i].pop(f)) break;

					f();
				}
			}

		public:
			task_system(int nthreads) : _count(nthreads), _threads(_count) {
				for (unsigned n = 0; n != _count; ++n) {
					_threads.emplace_back([&, n] { run(n); });
				}
			}

			task_system() : _count(thread::hardware_concurrency()), _threads(_count) {
				for (unsigned n = 0; n != _count; ++n) {
					_threads.emplace_back([&, n] { run(n); });
				}
			}

			~task_system() {
				for (auto& e : _q) e.done();
				for (auto& e : _threads) e.join();
			}

			template <typename F>
			void async_(F&& f) {
				auto i = _index++;

				for (unsigned n = 0; n != _count; ++n) {
					if (_q[(i + n) % _count].try_push(forward<F>(f))) return;
				}

				_q[i % _count].push(forward<F>(f));
			}
		};

		/**************************************************************************************************/

		task_system _system;

		/**************************************************************************************************/

		template <typename>
		struct result_of_;

		template <typename R, typename... Args>
		struct result_of_<R(Args...)> { using type = R; };

		template <typename F>
		using result_of_t_ = typename result_of_<F>::type;


		/**************************************************************************************************/

		template <typename R>
		struct shared_base {
			vector<R> _r; // optional
			mutex _mutex;
			condition_variable _ready;
			vector<function<void()>> _then;
			task_system *_system;

			shared_base() = delete;
			shared_base(task_system* system) : _system(system) {}
			virtual ~shared_base() { }

			void set(R&& r) {
				vector<function<void()>> then;
				{
					lock_t lock{ _mutex };
					_r.push_back(move(r));
					swap(_then, then);
				}
				_ready.notify_all();
				for (const auto& f : then) _system->async_(move(f));
			}

			template <typename F>
			void then(F&& f) {
				bool resolved{ false };
				{
					lock_t lock{ _mutex };
					if (_r.empty()) _then.push_back(forward<F>(f));
					else resolved = true;
				}
				if (resolved) _system->async_(move(f));
			}

			const R& get() {
				lock_t lock{ _mutex };
				while (_r.empty()) _ready.wait(lock);
				return _r.back();
			}
		};

		template <typename> struct shared; // not defined

		template <typename R, typename... Args>
		struct shared<R(Args...)> : shared_base<R> {
			function<R(Args...)> _f;

			shared() = delete;
			template<typename F>
			shared(F&& f, task_system* system) : _f(forward<F>(f)), shared_base(system) { }

			template <typename... A>
			void operator()(A&&... args) {
				this->set(_f(forward<A>(args)...));
				_f = nullptr;
			}
		};

		template <typename> class packaged_task; //not defined
		template <typename> class future;

		//template <typename S, typename F>
		//auto package(F&& f)->pair<packaged_task<S>, future<result_of_t_<S>>>;

		template <typename R>
		class future {
			shared_ptr<shared_base<R>> _p;

			//template <typename S, typename F>
			//friend auto package(F&& f)->pair<packaged_task<S>, future<result_of_t_<S>>>;
		public:
			explicit future(shared_ptr<shared_base<R>> p) : _p(move(p)) { }
		public:
			future() = default;

			template <typename F>
			auto then(F&& f) {
				auto pack = package<result_of_t<F(R)>()>([p = _p, f = forward<F>(f)](){
					return f(p->_r.back());
				});
				_p->then(move(pack.first));
				return pack.second;
			}

			const R& get() const { return _p->get(); }
		};

		template<typename R, typename ...Args >
		class packaged_task<R(Args...)> {
			weak_ptr<shared<R(Args...)>> _p;

			//template <typename S, typename F>
			//friend auto package(F&& f)->pair<packaged_task<S>, future<result_of_t_<S>>>;
		public:
			explicit packaged_task(weak_ptr<shared<R(Args...)>> p) : _p(move(p)) { }

		public:
			packaged_task() = default;

			template <typename... A>
			void operator()(A&&... args) const {
				auto p = _p.lock();
				if (p) (*p)(forward<A>(args)...);
			}
		};

		template <typename S, typename F>
		auto package(F&& f) -> pair<packaged_task<S>, future<result_of_t_<S>>> {
			auto p = make_shared<shared<S>>(forward<F>(f), reinterpret_cast<task_system*>(this));
			return make_pair(packaged_task<S>(p), future<result_of_t_<S>>(p));
		}

		template <typename F, typename ...Args>
		auto async(F&& f, Args&&... args)
		{
			using result_type = result_of_t<F(Args...)>;
			using packaged_type = packaged_task<result_type()>;

			auto pack = package<result_type()>(bind(forward<F>(f), forward<Args>(args)...));

			_system.async_(move(get<0>(pack)));
			return get<1>(pack);
		}
	};

}

/* original:
using lock_t = unique_lock<mutex>;

class notification_queue {
deque<function<void()>> _q;
bool                    _done{false};
mutex                   _mutex;
condition_variable      _ready;

public:
bool try_pop(function<void()>& x) {
lock_t lock{_mutex, try_to_lock};
if (!lock || _q.empty()) return false;
x = move(_q.front());
_q.pop_front();
return true;
}

template<typename F>
bool try_push(F&& f) {
{
lock_t lock{_mutex, try_to_lock};
if (!lock) return false;
_q.emplace_back(forward<F>(f));
}
_ready.notify_one();
return true;
}

void done() {
{
unique_lock<mutex> lock{_mutex};
_done = true;
}
_ready.notify_all();
}

bool pop(function<void()>& x) {
lock_t lock{_mutex};
while (_q.empty() && !_done) _ready.wait(lock);
if (_q.empty()) return false;
x = move(_q.front());
_q.pop_front();
return true;
}

template<typename F>
void push(F&& f) {
{
lock_t lock{_mutex};
_q.emplace_back(forward<F>(f));
}
_ready.notify_one();
}
};


class task_system {
	const unsigned              _count{ thread::hardware_concurrency() };
	vector<thread>              _threads;
	vector<notification_queue>  _q{ _count };
	atomic<unsigned>            _index{ 0 };

	void run(unsigned i) {
		while (true) {
			function<void()> f;

			for (unsigned n = 0; n != _count * 32; ++n) {
				if (_q[(i + n) % _count].try_pop(f)) break;
			}
			if (!f && !_q[i].pop(f)) break;

			f();
		}
	}

public:
	task_system() {
		for (unsigned n = 0; n != _count; ++n) {
			_threads.emplace_back([&, n] { run(n); });
		}
	}

	~task_system() {
		for (auto& e : _q) e.done();
		for (auto& e : _threads) e.join();
	}

	template <typename F>
	void async_(F&& f) {
		auto i = _index++;

		for (unsigned n = 0; n != _count; ++n) {
			if (_q[(i + n) % _count].try_push(forward<F>(f))) return;
		}

		_q[i % _count].push(forward<F>(f));
	}
};


task_system _system;


template <typename>
struct result_of_;

template <typename R, typename... Args>
struct result_of_<R(Args...)> { using type = R; };

template <typename F>
using result_of_t_ = typename result_of_<F>::type;

template <typename R>
struct shared_base {
	vector<R> _r; // optional
	mutex _mutex;
	condition_variable _ready;
	vector<function<void()>> _then;

	virtual ~shared_base() { }

	void set(R&& r) {
		vector<function<void()>> then;
		{
			lock_t lock{ _mutex };
			_r.push_back(move(r));
			swap(_then, then);
		}
		_ready.notify_all();
		for (const auto& f : then) _system.async_(move(f));
	}

	template <typename F>
	void then(F&& f) {
		bool resolved{ false };
		{
			lock_t lock{ _mutex };
			if (_r.empty()) _then.push_back(forward<F>(f));
			else resolved = true;
		}
		if (resolved) _system.async_(move(f));
	}

	const R& get() {
		lock_t lock{ _mutex };
		while (_r.empty()) _ready.wait(lock);
		return _r.back();
	}
};

template <typename> struct shared; // not defined

template <typename R, typename... Args>
struct shared<R(Args...)> : shared_base<R> {
	function<R(Args...)> _f;

	template<typename F>
	shared(F&& f) : _f(forward<F>(f)) { }

	template <typename... A>
	void operator()(A&&... args) {
		this->set(_f(forward<A>(args)...));
		_f = nullptr;
	}
};

template <typename> class packaged_task; //not defined
template <typename> class future;

template <typename S, typename F>
auto package(F&& f)->pair<packaged_task<S>, future<result_of_t_<S>>>;

template <typename R>
class future {
	shared_ptr<shared_base<R>> _p;

	template <typename S, typename F>
	friend auto package(F&& f)->pair<packaged_task<S>, future<result_of_t_<S>>>;

	explicit future(shared_ptr<shared_base<R>> p) : _p(move(p)) { }
public:
	future() = default;

	template <typename F>
	auto then(F&& f) {
		auto pack = package<result_of_t<F(R)>()>([p = _p, f = forward<F>(f)](){
			return f(p->_r.back());
		});
		_p->then(move(pack.first));
		return pack.second;
	}

	const R& get() const { return _p->get(); }
};

template<typename R, typename ...Args >
class packaged_task<R(Args...)> {
	weak_ptr<shared<R(Args...)>> _p;

	template <typename S, typename F>
	friend auto package(F&& f)->pair<packaged_task<S>, future<result_of_t_<S>>>;

	explicit packaged_task(weak_ptr<shared<R(Args...)>> p) : _p(move(p)) { }

public:
	packaged_task() = default;

	template <typename... A>
	void operator()(A&&... args) const {
		auto p = _p.lock();
		if (p) (*p)(forward<A>(args)...);
	}
};

template <typename S, typename F>
auto package(F&& f) -> pair<packaged_task<S>, future<result_of_t_<S>>> {
	auto p = make_shared<shared<S>>(forward<F>(f));
	return make_pair(packaged_task<S>(p), future<result_of_t_<S>>(p));
}

template <typename F, typename ...Args>
auto async(F&& f, Args&&... args)
{
	using result_type = result_of_t<F(Args...)>;
	using packaged_type = packaged_task<result_type()>;

	auto pack = package<result_type()>(bind(forward<F>(f), forward<Args>(args)...));

	_system.async_(move(get<0>(pack)));
	return get<1>(pack);
}

*/