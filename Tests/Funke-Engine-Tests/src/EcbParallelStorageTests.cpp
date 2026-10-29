#include <doctest/doctest.h>
#include <entt/entity/registry.hpp>
#include <entt/entity/storage.hpp>
#include <entt/signal/sigh.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

	struct BulkPayload {
		inline static std::atomic<int> Live{ 0 };
		int Value;
		std::string Text;

		explicit BulkPayload(int value)
			: Value(value), Text(std::to_string(value) + std::string(96, 'x')) {
			Live.fetch_add(1, std::memory_order_relaxed);
		}

		BulkPayload(BulkPayload&& other) noexcept
			: Value(std::exchange(other.Value, -1)), Text(std::move(other.Text)) {
			Live.fetch_add(1, std::memory_order_relaxed);
		}

		BulkPayload& operator=(BulkPayload&& other) noexcept {
			Value = std::exchange(other.Value, -1);
			Text = std::move(other.Text);
			return *this;
		}

		~BulkPayload() { Live.fetch_sub(1, std::memory_order_relaxed); }
	};

	struct SerialBulkExecutor {
		template <typename F>
		void operator()(size_t count, F&& work) const noexcept {
			for (size_t index = 0; index < count; ++index) work(index);
		}
	};

	struct ConcurrentBulkExecutor {
		std::atomic<size_t>& WorkerCalls;

		template <typename F>
		void operator()(size_t count, F&& work) const noexcept {
			std::thread worker;
			try {
				worker = std::thread([&] {
					for (size_t index = 0; index < count; index += 2) {
						work(index);
						WorkerCalls.fetch_add(1, std::memory_order_relaxed);
					}
				});
			}
			catch (...) {
				SerialBulkExecutor{}(count, work);
				return;
			}
			for (size_t index = 1; index < count; index += 2) work(index);
			worker.join();
		}
	};

	struct SignalObserver {
		int Calls = 0;
		void OnValue(int) { ++Calls; }
		void OnOther(int) { Calls += 2; }
		void OnComponent(entt::registry&, entt::entity) { ++Calls; }
	};

	void FreeSignalListener(int) {}

	struct AllocationState {
		int Remaining = -1;
	};

	template <typename T>
	struct FailingBulkAllocator {
		using value_type = T;
		AllocationState* State = nullptr;

		FailingBulkAllocator() noexcept = default;
		explicit FailingBulkAllocator(AllocationState& state) noexcept : State(&state) {}
		template <typename U>
		FailingBulkAllocator(const FailingBulkAllocator<U>& other) noexcept : State(other.State) {}

		T* allocate(size_t count) {
			if (State != nullptr && State->Remaining >= 0) {
				if (State->Remaining == 0) throw std::bad_alloc{};
				--State->Remaining;
			}
			return std::allocator<T>{}.allocate(count);
		}

		void deallocate(T* pointer, size_t count) noexcept {
			std::allocator<T>{}.deallocate(pointer, count);
		}

		template <typename U>
		bool operator==(const FailingBulkAllocator<U>& other) const noexcept {
			return State == other.State;
		}
	};

	void CheckEquivalent(const entt::storage<BulkPayload>& actual,
		const entt::storage<BulkPayload>& expected) {
		REQUIRE(actual.size() == expected.size());
		for (size_t index = 0; index < expected.size(); ++index) {
			const auto entity = expected.data()[index];
			REQUIRE(actual.data()[index] == entity);
			CHECK(actual.get(entity).Value == expected.get(entity).Value);
			CHECK(actual.get(entity).Text == expected.get(entity).Text);
		}
	}

}

TEST_CASE("ECB bulk storage inserts and removes payloads on workers without publishing signals") {
	REQUIRE(BulkPayload::Live.load() == 0);
	{
		SignalObserver observer;
		entt::registry registry;
		auto& storage = registry.storage<BulkPayload>();
		storage.on_construct().connect<&SignalObserver::OnComponent>(&observer);
		storage.on_destroy().connect<&SignalObserver::OnComponent>(&observer);
		auto& raw = static_cast<entt::storage<BulkPayload>&>(storage);
		const auto existing = registry.create();
		storage.emplace(existing, -17);
		REQUIRE(observer.Calls == 1);

		constexpr size_t count = 4097;
		std::vector<entt::entity> entities(count);
		registry.create(entities.begin(), entities.end());
		std::vector<BulkPayload> values;
		values.reserve(count);
		for (size_t index = 0; index < count; ++index) values.emplace_back(static_cast<int>(index));
		std::atomic<size_t> workerCalls{ 0 };
		ConcurrentBulkExecutor execute{ workerCalls };
		raw.bulk_insert(entities.begin(), entities.end(), execute,
			[&](BulkPayload* row, size_t index) noexcept {
				std::construct_at(row, std::move(values[index]));
			});

		CHECK(workerCalls.load() > 0);
		CHECK(observer.Calls == 1);
		CHECK(raw.size() == count + 1);
		CHECK(raw.get(existing).Value == -17);
		for (size_t index = 0; index < count; ++index) {
			CHECK(raw.get(entities[index]).Value == static_cast<int>(index));
			CHECK(raw.get(entities[index]).Text == std::to_string(index) + std::string(96, 'x'));
		}

		std::mt19937 random(0x3711);
		std::shuffle(entities.begin(), entities.end(), random);
		const auto beforeRemoval = workerCalls.load();
		raw.bulk_remove(entities.begin(), entities.begin() + count / 2, execute);
		CHECK(workerCalls.load() > beforeRemoval);
		CHECK(observer.Calls == 1);
		CHECK(raw.get(existing).Value == -17);
		for (size_t index = 0; index < count; ++index) {
			CHECK(raw.contains(entities[index]) == (index >= count / 2));
		}
		CHECK(BulkPayload::Live.load() == static_cast<int>(values.size() + raw.size()));

		storage.on_construct().disconnect();
		storage.on_destroy().disconnect();
	}
	CHECK(BulkPayload::Live.load() == 0);
}

TEST_CASE("ECB bulk storage removal matches sequential packed order across randomized batches") {
	REQUIRE(BulkPayload::Live.load() == 0);
	std::mt19937 random(0x4B771);
	for (size_t round = 0; round < 60; ++round) {
		const size_t count = 1 + random() % 321;
		entt::storage<BulkPayload> expected;
		entt::storage<BulkPayload> actual;
		std::vector<entt::entity> entities;
		for (size_t index = 0; index < count; ++index) {
			const auto entity = entt::entt_traits<entt::entity>::construct(
				static_cast<uint32_t>(index), static_cast<uint32_t>(round % 7));
			entities.push_back(entity);
			expected.emplace(entity, static_cast<int>(index));
			actual.emplace(entity, static_cast<int>(index));
		}
		std::shuffle(entities.begin(), entities.end(), random);

		for (size_t offset = 0; offset < count;) {
			const size_t batch = 1 + random() % (count - offset);
			auto first = entities.begin() + offset;
			auto last = first + batch;
			for (auto position = first; position != last; ++position) expected.remove(*position);
			actual.bulk_remove(first, last, SerialBulkExecutor{});
			CheckEquivalent(actual, expected);
			for (auto position = first; position != last; ++position) CHECK_FALSE(actual.contains(*position));
			offset += batch;
			CHECK(BulkPayload::Live.load() == static_cast<int>(expected.size() + actual.size()));
		}

		const auto reused = entt::entt_traits<entt::entity>::construct(0, 17);
		actual.emplace(reused, 991);
		CHECK(actual.get(reused).Value == 991);
		CHECK_FALSE(actual.contains(entities.front()));
	}
	CHECK(BulkPayload::Live.load() == 0);
}

TEST_CASE("ECB bulk storage removal accepts aliased packed ranges and empty batches") {
	REQUIRE(BulkPayload::Live.load() == 0);
	{
		entt::storage<BulkPayload> storage;
		for (uint32_t index = 0; index < 129; ++index) storage.emplace(static_cast<entt::entity>(index), index);
		const auto* first = storage.data();
		storage.bulk_remove(first, first, SerialBulkExecutor{});
		CHECK(storage.size() == 129);
		storage.bulk_remove(first, first + 129, SerialBulkExecutor{});
		CHECK(storage.empty());
		CHECK(BulkPayload::Live.load() == 0);
		std::array<entt::entity, 0> empty;
		storage.bulk_insert(empty.begin(), empty.end(), SerialBulkExecutor{},
			[](BulkPayload*, size_t) noexcept {});
		CHECK(storage.empty());
	}
	CHECK(BulkPayload::Live.load() == 0);
}

TEST_CASE("ECB prepared bulk removal leaves storage intact until its nonthrowing commit") {
	REQUIRE(BulkPayload::Live.load() == 0);
	{
		entt::storage<BulkPayload> expected;
		entt::storage<BulkPayload> actual;
		for (uint32_t index = 0; index < 67; ++index) {
			const auto entity = static_cast<entt::entity>(index);
			expected.emplace(entity, index);
			actual.emplace(entity, index);
		}
		const std::array removed{ static_cast<entt::entity>(4), static_cast<entt::entity>(65),
			static_cast<entt::entity>(17), static_cast<entt::entity>(66), static_cast<entt::entity>(0) };
		auto plan = actual.prepare_bulk_remove(removed.begin(), removed.end());
		CheckEquivalent(actual, expected);
		CHECK(BulkPayload::Live.load() == 134);
		static_assert(noexcept(actual.bulk_remove(plan, SerialBulkExecutor{})));
		for (auto entity : removed) expected.remove(entity);
		actual.bulk_remove(plan, SerialBulkExecutor{});
		CheckEquivalent(actual, expected);
		CHECK(BulkPayload::Live.load() == 124);
	}
	CHECK(BulkPayload::Live.load() == 0);
}

TEST_CASE("ECB bulk insertion rolls back sparse preparation after allocation failures") {
	REQUIRE(BulkPayload::Live.load() == 0);
	int failures = 0;
	int successes = 0;
	for (int allowance = 0; allowance < 24; ++allowance) {
		AllocationState state;
		using Allocator = FailingBulkAllocator<BulkPayload>;
		entt::basic_storage<BulkPayload, entt::entity, Allocator> storage{ Allocator{ state } };
		const auto existing = static_cast<entt::entity>(0);
		storage.emplace(existing, 731);
		std::vector<entt::entity> entities;
		std::vector<BulkPayload> values;
		for (uint32_t index = 0; index < 6; ++index) {
			entities.push_back(static_cast<entt::entity>(1 + index * 8192));
			values.emplace_back(static_cast<int>(index));
		}
		state.Remaining = allowance;
		size_t initialized = 0;
		try {
			storage.bulk_insert(entities.begin(), entities.end(), SerialBulkExecutor{},
				[&](BulkPayload* row, size_t index) noexcept {
					std::construct_at(row, std::move(values[index]));
					++initialized;
				});
			++successes;
			CHECK(initialized == entities.size());
			CHECK(storage.size() == entities.size() + 1);
		}
		catch (const std::bad_alloc&) {
			++failures;
			CHECK(initialized == 0);
			CHECK(storage.size() == 1);
			for (auto entity : entities) CHECK_FALSE(storage.contains(entity));
			state.Remaining = -1;
			storage.emplace(entities.front(), 997);
			CHECK(storage.get(entities.front()).Value == 997);
		}
		CHECK(storage.get(existing).Value == 731);
		CHECK(BulkPayload::Live.load() == static_cast<int>(values.size() + storage.size()));
	}
	CHECK(failures > 1);
	CHECK(successes > 0);
	CHECK(BulkPayload::Live.load() == 0);
}

TEST_CASE("Signal only identifies the exact sole callback and bound instance") {
	entt::sigh<void(int)> signal;
	entt::sink sink{ signal };
	SignalObserver first;
	SignalObserver second;
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&first));
	sink.connect<&SignalObserver::OnValue>(&first);
	CHECK(sink.only<&SignalObserver::OnValue>(&first));
	CHECK(sink.only<&SignalObserver::OnValue>(first));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&second));
	CHECK_FALSE(sink.only<&SignalObserver::OnOther>(&first));
	CHECK_FALSE(sink.only<&SignalObserver::OnOther>(first));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(static_cast<SignalObserver*>(nullptr)));
	sink.connect<&SignalObserver::OnValue>(&second);
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&first));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&second));
	signal.publish(1);
	CHECK(first.Calls == 1);
	CHECK(second.Calls == 1);
	sink.disconnect<&SignalObserver::OnValue>(&first);
	CHECK(sink.only<&SignalObserver::OnValue>(&second));
	sink.disconnect();
	sink.connect<&FreeSignalListener>();
	CHECK(sink.only<&FreeSignalListener>());
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&second));
	sink.disconnect();
	sink.connect<&SignalObserver::OnValue>(first);
	CHECK(sink.only<&SignalObserver::OnValue>(first));
	CHECK(sink.only<&SignalObserver::OnValue>(&first));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(second));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&second));
	CHECK_FALSE(sink.only<&SignalObserver::OnOther>(first));
	CHECK_FALSE(sink.only<&SignalObserver::OnOther>(&first));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(static_cast<SignalObserver*>(nullptr)));
	sink.connect<&SignalObserver::OnOther>(second);
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(first));
	CHECK_FALSE(sink.only<&SignalObserver::OnValue>(&first));
}
