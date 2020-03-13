#define BOOST_TEST_MODULE TransactionTrackerTest
#include <boost/test/unit_test.hpp>
#include <deque>
#include <fc/reflect/variant.hpp>
#include <fc/static_variant.hpp>
#include <memory>

#include <iostream>

namespace eosio { namespace chain {

using transaction_id_type = std::string;

struct block_timestamp_type {
   block_timestamp_type() = default;
   block_timestamp_type(uint32_t s): slot(s) {}
   block_timestamp_type operator+(int x) const { return block_timestamp_type{this->slot + x}; }
   block_timestamp_type operator-(int x) const { return block_timestamp_type{this->slot - x}; }
   uint32_t             slot;
};



struct transaction {
   transaction_id_type id() const { return _id; };
   transaction_id_type _id;
   uint16_t            ref_block_num    = 0U;
   uint32_t            ref_block_prefix = 0U;
};


struct packed_transaction {
   packed_transaction(transaction_id_type tid, uint16_t ref_block_num, uint32_t ref_block_prefix)
       : _transaction{tid, ref_block_num, ref_block_prefix} {}
   const transaction& get_transaction() const { return _transaction; }
   transaction        _transaction;
};


struct transaction_receipt {
   transaction_receipt(transaction_id_type tid, uint16_t ref_block_num, uint32_t ref_block_prefix)
       : trx(packed_transaction{tid, ref_block_num, ref_block_prefix}) {}
   fc::static_variant<transaction_id_type, packed_transaction> trx;
};


struct signed_block {
   uint32_t                        block_num() const { return _block_num; };
   uint32_t                        _block_num;

   std::deque<transaction_receipt> transactions;
};

using signed_block_ptr = std::shared_ptr<signed_block>;
struct block_state {
   struct header_t {
      block_timestamp_type            timestamp;
   } header;
   signed_block_ptr block;
};

using block_state_ptr = std::shared_ptr<block_state>;
} // nammespace chain
} // namespace eosio
#include "../transaction_tracker.hpp"

struct response_state_t {
   int         handler_called = 0;
   int         status         = 0;
   std::string msg;
};

using namespace eosio;

eosio::block_state_ptr make_block_state(uint32_t block_num, block_timestamp_type timestamp,
                                        std::initializer_list<transaction_receipt> transactions) {
   auto block_ptr = std::make_shared<signed_block>(signed_block{block_num, transactions});
   return std::make_shared<block_state>(block_state{{timestamp}, block_ptr});
}

template <typename Tracker>
struct tracker_test_fixture {
   Tracker                    tracker;
   const block_timestamp_type base_timestamp{1000};
   const uint32_t             base_block_num = 500;
   response_state_t           response_state;
   url_response_callback      response_handler;
   tracker_test_fixture() {
      tracker.on_irreversible_block(make_block_state(base_block_num, base_timestamp, {}));
      response_handler = [this](int status, fc::variant msg) {
         response_state.handler_called += 1;
         response_state.status = status;
         response_state.msg    = fc::json::to_string(msg, fc::time_point::maximum());
      };
   }

   void test_wait_before_accepted() {
      // When received a wait request for accepted block
      tracker.handle_wait_transaction_request(
          "", R"({"transaction_id":"trx100", "condition":"accepted", "timeout":180})", response_handler);

      // then the response handler shouldn't be called
      BOOST_CHECK_EQUAL(response_state.handler_called, 0);

      // and when an accepted block received which does not contain the transaction being requested
      tracker.on_accepted_block(make_block_state(base_block_num + 100, base_timestamp + 100, {{"trx1", 1, 2}}));

      // then no response is sent
      BOOST_CHECK_EQUAL(response_state.handler_called, 0);
      tracker.on_irreversible_block(make_block_state(base_block_num + 1, base_timestamp + 1, {}));
      // and when an accepted block received which contain the transaction being requested
      tracker.on_accepted_block(make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx100", 11, 22}}));

      // then the response will be sent
      BOOST_CHECK_EQUAL(response_state.handler_called, 1);
      BOOST_CHECK_EQUAL(response_state.status, 202);
      BOOST_CHECK_EQUAL(response_state.msg, R"({"block_num":601,"ref_block_num":11,"ref_block_prefix":22})");

      BOOST_CHECK(tracker.contain_transaction("trx100"));
   }

   void test_wait_after_accepted() {
      // when trx100 is accepted
      tracker.on_accepted_block(make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx100", 11, 22}}));

      // and the wait request is received after it
      tracker.handle_wait_transaction_request(
          "", R"({"transaction_id":"trx100", "condition":"accepted", "timeout":180})", response_handler);

      // then the response will be sent
      BOOST_CHECK_EQUAL(response_state.handler_called, 1);
      BOOST_CHECK_EQUAL(response_state.status, 202);
      BOOST_CHECK_EQUAL(response_state.msg, R"({"block_num":601,"ref_block_num":11,"ref_block_prefix":22})");
   }

   void test_wait_before_finalized() {
      // When received a wait request for finalized block
      tracker.handle_wait_transaction_request(
          "", R"({"transaction_id":"trx100", "condition":"finalized", "timeout":180})", response_handler);

      // and when the transaction is accepted
      tracker.on_accepted_block(make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx100", 11, 22}}));
      // then the response handler shouldn't be called
      BOOST_CHECK_EQUAL(response_state.handler_called, 0);

      // When the trantraction becomes irreversible
      tracker.on_irreversible_block(make_block_state(base_block_num + 102, base_timestamp + 102, {{"trx100", 11, 22}}));
      // then the response handler should be called
      BOOST_CHECK_EQUAL(response_state.handler_called, 1);
      BOOST_CHECK_EQUAL(response_state.status, 201);
      BOOST_CHECK_EQUAL(response_state.msg, R"({"block_num":602,"ref_block_num":11,"ref_block_prefix":22})");
   }

   void test_wait_after_finalized() {

      // and when the transaction is accepted
      tracker.on_accepted_block(make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx100", 11, 22}}));
      // then the response handler shouldn't be called
      BOOST_CHECK_EQUAL(response_state.handler_called, 0);

      // When the trantraction becomes irreversible
      tracker.on_irreversible_block(make_block_state(base_block_num + 102, base_timestamp + 102, {{"trx100", 11, 22}}));
      // then the response handler shouldn't be called
      BOOST_CHECK_EQUAL(response_state.handler_called, 0);

      // When received a wait request for finalized block
      tracker.handle_wait_transaction_request(
          "", R"({"transaction_id":"trx100", "condition":"finalized", "timeout":180})", response_handler);

      // then the response handler should be called
      BOOST_CHECK_EQUAL(response_state.handler_called, 1);
      BOOST_CHECK_EQUAL(response_state.status, 201);
      BOOST_CHECK_EQUAL(response_state.msg, R"({"block_num":602,"ref_block_num":11,"ref_block_prefix":22})");
   }
};

BOOST_AUTO_TEST_SUITE(test_global_transaction_tracker)

using test_fixture = tracker_test_fixture<global_transaction_tracker>;

BOOST_FIXTURE_TEST_CASE(test_invalid_wait, test_fixture) {
   // When received a wait request with invalid wait condition
   tracker.handle_wait_transaction_request(
       "", R"({"transaction_id":"trx100", "condition":"accept", "timeout":180})", response_handler);

   BOOST_CHECK_EQUAL(response_state.handler_called, 1);
   BOOST_CHECK_EQUAL(response_state.status, 422);

   // reset the status
   response_state.status = 0;

   // When received a wait request with invalid format
   tracker.handle_wait_transaction_request("", R"({})", response_handler);

   BOOST_CHECK_EQUAL(response_state.handler_called, 2);
   BOOST_CHECK_EQUAL(response_state.status, 422);
   std::cout << response_state.msg << "\n";
}

BOOST_FIXTURE_TEST_CASE(test_wait_before_accepted, test_fixture) { test_wait_before_accepted(); }
BOOST_FIXTURE_TEST_CASE(test_wait_after_accepted, test_fixture) { test_wait_after_accepted(); }
BOOST_FIXTURE_TEST_CASE(test_wait_before_finalized, test_fixture) { test_wait_before_finalized(); }
BOOST_FIXTURE_TEST_CASE(test_wait_after_finalized, test_fixture) { test_wait_after_finalized(); }

BOOST_FIXTURE_TEST_CASE(test_wait_timeout, test_fixture) {

   // When received a wait request for finalized block
   tracker.handle_wait_transaction_request(
       "", R"({"transaction_id":"trx100", "condition":"finalized", "timeout":180})", response_handler);

   // and when the transaction is accepted
   tracker.on_accepted_block(make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx100", 11, 22}}));
   // then the response handler shouldn't be called
   BOOST_CHECK_EQUAL(response_state.handler_called, 0);

   BOOST_CHECK(tracker.contain_transaction("trx100"));

   // and when the transaction is not finalized before
   tracker.on_irreversible_block(make_block_state(base_block_num + 179, base_timestamp + 359, {}));
   BOOST_CHECK_EQUAL(response_state.handler_called, 0);
   BOOST_CHECK(tracker.contain_transaction("trx100"));

   tracker.on_irreversible_block(make_block_state(base_block_num + 180, base_timestamp + 361, {}));
   BOOST_CHECK_EQUAL(response_state.handler_called, 1);
   BOOST_CHECK(!tracker.contain_transaction("trx100"));
}

BOOST_FIXTURE_TEST_CASE(test_transaction_clean_up, test_fixture) {

   const uint32_t start_lib_slot = tracker.current_lib_slot();

   tracker.handle_wait_transaction_request(
       "", R"({"transaction_id":"trx100", "condition":"finalized", "timeout":180})", response_handler);
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx100"), start_lib_slot + 360);

   tracker.on_accepted_block(
       make_block_state(base_block_num + 100, base_timestamp + 100, {{"trx100", 11, 22}, {"trx101", 22, 33}}));
   // make sure the expiration slot of the transaction that has been waited won't change after accepted
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx100"), start_lib_slot + 360);
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx101"), start_lib_slot + tracker.num_slots_pass_lib);
   tracker.on_accepted_block(
       make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx102", 11, 22}, {"trx103", 22, 33}}));
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx102"), start_lib_slot + tracker.num_slots_pass_lib);
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx103"), start_lib_slot + tracker.num_slots_pass_lib);

   tracker.on_irreversible_block(
       make_block_state(base_block_num + 103, base_timestamp + 103, {{"trx100", 11, 22}, {"trx101", 22, 33}}));
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx100"),
                     start_lib_slot + 103 + tracker.num_slots_pass_lib);
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx101"),
                     start_lib_slot + 103 + tracker.num_slots_pass_lib);
   tracker.on_irreversible_block(
       make_block_state(base_block_num + 104, base_timestamp + 104, {{"trx102", 11, 22}, {"trx103", 22, 33}}));
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx102"),
                     start_lib_slot + 104 + tracker.num_slots_pass_lib);
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx103"),
                     start_lib_slot + 104 + tracker.num_slots_pass_lib);

   // after receiving the block passed the expiration times of trx100, trx101, trx102 and trx103
   tracker.on_irreversible_block(make_block_state(base_block_num + 104 + tracker.num_slots_pass_lib,
                                                  base_timestamp + 104 + tracker.num_slots_pass_lib, {}));
   // all the mentioned transaction do not exists in the tracker anymore.
   BOOST_CHECK(!tracker.contain_transaction("trx100"));
   BOOST_CHECK(!tracker.contain_transaction("trx101"));
   BOOST_CHECK(!tracker.contain_transaction("trx102"));
   BOOST_CHECK(!tracker.contain_transaction("trx103"));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(test_local_transaction_tracker)

using test_fixture = tracker_test_fixture<local_transaction_tracker>;

BOOST_FIXTURE_TEST_CASE(test_no_add_before_wait_accepted_and_finalized, test_fixture) {
   // When received a wait request for accepted block
   tracker.handle_wait_transaction_request(
       "", R"({"transaction_id":"trx100", "condition":"accepted"})", response_handler);
   // then the response will be sent
   BOOST_CHECK_EQUAL(response_state.handler_called, 1);
   BOOST_CHECK_EQUAL(response_state.status, 404);

   // when trx100 is accepted
   tracker.on_accepted_block(make_block_state(base_block_num + 101, base_timestamp + 101, {{"trx100", 11, 22}}));
   BOOST_CHECK(!tracker.contain_transaction("trx100"));

   // When the trantraction becomes irreversible
   tracker.on_irreversible_block(make_block_state(base_block_num + 102, base_timestamp + 102, {{"trx100", 11, 22}}));
   BOOST_CHECK(!tracker.contain_transaction("trx100"));
}

BOOST_FIXTURE_TEST_CASE(test_wait_before_accepted, test_fixture) {
   tracker.add("trx100");
   test_wait_before_accepted();
}

BOOST_FIXTURE_TEST_CASE(test_wait_after_accepted, test_fixture) {
   tracker.add("trx100");
   test_wait_after_accepted();
}

BOOST_FIXTURE_TEST_CASE(test_wait_before_finalized, test_fixture) {
   tracker.add("trx100");
   test_wait_before_finalized();
}

BOOST_FIXTURE_TEST_CASE(test_wait_after_finalized, test_fixture) {
   tracker.add("trx100");
   test_wait_after_finalized();
}

BOOST_FIXTURE_TEST_CASE(test_transaction_expire, test_fixture) {
   // for local_transaction_tracker, the expiration of a transaction is calculated based on the lib slot at the time of the transaction is sent.
   const uint32_t start_lib_slot = tracker.current_lib_slot();
   const uint32_t trx100_expiration_slot = start_lib_slot + tracker.num_slots_pass_lib;
   tracker.add("trx100");
   BOOST_REQUIRE(tracker.contain_transaction("trx100"));
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx100"), trx100_expiration_slot);

   // When received a wait request for accepted block
   tracker.handle_wait_transaction_request(
       "", R"({"transaction_id":"trx100", "condition":"accepted"})", response_handler);

   BOOST_CHECK_EQUAL(response_state.handler_called, 0);
   BOOST_CHECK(tracker.contain_transaction("trx100"));
   // the wait request has no effect on the expiration of the transaction
   BOOST_CHECK_EQUAL(tracker.get_transaction_expiration_slot("trx100"), trx100_expiration_slot);


   // after receiving the block passed the expiration times of trx100
   tracker.on_irreversible_block(make_block_state(1000,
                                                  trx100_expiration_slot, {}));

   BOOST_CHECK_EQUAL(response_state.handler_called, 1);
   BOOST_CHECK_EQUAL(response_state.status, 504);
   BOOST_CHECK(!tracker.contain_transaction("trx100"));

}

BOOST_AUTO_TEST_SUITE_END()
