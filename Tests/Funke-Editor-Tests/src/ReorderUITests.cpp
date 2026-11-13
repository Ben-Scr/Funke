#include <doctest/doctest.h>
#include "Gui/ReorderUI.hpp"
#include <string>

TEST_CASE("Reorder insertion gaps preserve the previewed position in both directions") {
	const auto move = [](int source, int slot) {
		std::string order = "ABCDE";
		const int destination = Funke::ReorderUI::DestinationIndex(source, slot, static_cast<int>(order.size()));
		if (destination >= 0) {
			const char item = order[source];
			order.erase(source, 1);
			order.insert(order.begin() + destination, item);
		}
		return order;
	};
	CHECK(move(1, 0) == "BACDE");  // before first
	CHECK(move(1, 3) == "ACBDE");  // below C when moving down
	CHECK(move(3, 1) == "ADBCE");  // above B when moving up
	CHECK(move(1, 5) == "ACDEB");  // after last / empty tail
	CHECK(move(1, 1) == "ABCDE");  // own leading gap
	CHECK(move(1, 2) == "ABCDE");  // own trailing gap
}

TEST_CASE("Reorder insertion rejects stale indices and handles single rows") {
	using Funke::ReorderUI::DestinationIndex;
	CHECK(DestinationIndex(-1, 0, 3) == -1);
	CHECK(DestinationIndex(3, 0, 3) == -1);
	CHECK(DestinationIndex(0, -1, 3) == -1);
	CHECK(DestinationIndex(0, 4, 3) == -1);
	CHECK(DestinationIndex(0, 0, 0) == -1);
	CHECK(DestinationIndex(0, 0, 1) == 0);
	CHECK(DestinationIndex(0, 1, 1) == 0);
}
