
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <fc/io/json.hpp>
#include <fc/log/log_message.hpp>
#include <fc/variant.hpp>
#include <functional>
#include <vector>

namespace eosio {

using namespace chain;

struct wait_transaction_params {
   transaction_id_type transaction_id;
   std::string         condition;         ///< Must be either "ACCEPTED" or "FINALIZED"
   uint32_t            timeout; ///< the duration for a wait_transaction request to expire
};

struct wait_response_t {
   uint32_t block_num        = 0U;
   uint16_t ref_block_num    = 0U;
   uint32_t ref_block_prefix = 0U;
};

struct error_result_t {
   uint16_t    code;
   std::string message;

   struct error_t {
      struct detail_t {
         std::string file;
         uint64_t    line_number;
         std::string method;
      };
      std::vector<detail_t> details;
   } error;
};

} // namespace eosio

FC_REFLECT(eosio::wait_transaction_params, (transaction_id)(condition)(timeout))
FC_REFLECT(eosio::wait_response_t, (block_num)(ref_block_num)(ref_block_prefix))
FC_REFLECT(eosio::error_result_t::error_t::detail_t, (file)(line_number)(method))
FC_REFLECT(eosio::error_result_t::error_t, (details))
FC_REFLECT(eosio::error_result_t, (code)(message)(error))

namespace eosio {

#define MAKE_ERROR_RESULT(code, message) fc::variant(error_result_t{code, message, {{{__FILE__, __LINE__, __func__}}}})

using url_response_callback = std::function<void(int, fc::variant)>;

enum class trx_condition_t { NONE, FINALIZED = 201, ACCEPTED = 202, INVALID = 422 };

trx_condition_t parse_condition(std::string cond) {
   if (cond == "accepted")
      return trx_condition_t::ACCEPTED;
   if (cond == "finalized")
      return trx_condition_t::FINALIZED;
   return trx_condition_t::INVALID;
}

struct tracked_transaction_state {
   transaction_id_type   id;
   uint32_t              expiration_slot; ///< the lib slot for this record to expire
   trx_condition_t       wait_condition = trx_condition_t::NONE;
   trx_condition_t       result_status  = trx_condition_t::NONE;
   wait_response_t       response;
   url_response_callback wait_cb;

   tracked_transaction_state(transaction_id_type tid, uint32_t exp = 0U)
       : id(tid)
       , expiration_slot(exp) {}

   void on_wait_request(trx_condition_t request_condition, const url_response_callback& cb) {
      if (request_condition == result_status) {
         cb(static_cast<int>(this->result_status), fc::variant(response));
         return;
      }

      if (wait_cb) {
         cb(403, MAKE_ERROR_RESULT(403, "pending wait on the transaction exists"));
      } else {
         wait_cb        = cb;
         wait_condition = request_condition;
      }
   }

   void on_block(trx_condition_t condition, uint32_t block_num, const transaction& trx) {
      this->result_status             = condition;
      this->response.block_num        = block_num;
      this->response.ref_block_num    = trx.ref_block_num;
      this->response.ref_block_prefix = trx.ref_block_prefix;

      if (this->wait_condition == condition && wait_cb) {
         wait_cb(static_cast<int>(this->result_status), fc::variant(response));
         wait_cb = url_response_callback{};
      }
   }

   void on_expired() const {
      if (wait_cb) {
         wait_cb(504, MAKE_ERROR_RESULT(504, "wait transaction expired"));
      }
   }
};

using namespace boost::multi_index;

using tracked_transactions_t =
    multi_index_container<tracked_transaction_state,
                          indexed_by<hashed_unique<key<&tracked_transaction_state::id>>,
                                     ordered_non_unique<key<&tracked_transaction_state::expiration_slot>>>>;

class transaction_tracker {
 public:
   tracked_transactions_t tracked_transactions;
   uint32_t               lib_slot = 0U; ///< the slot for the last irreversible block

 public:
   const uint32_t num_slots_pass_lib; ///< the duration (in 0.5 second) for a transaction to be kept in memory
                                      ///< after it has been incorporated into an irreversible block

   transaction_tracker(uint32_t sec_pass_lib = 600)
       : num_slots_pass_lib(sec_pass_lib * 2) {}

   virtual ~transaction_tracker() {}
   virtual void add(const transaction_id_type& id){};
   virtual void set_tracked_transaction(trx_condition_t status, uint32_t block_num, const transaction& trx) = 0;

   virtual void on_wait_request(transaction_id_type transaction_id, trx_condition_t condition,
                                uint32_t timeout, url_response_callback cb) = 0;

   void handle_wait_transaction_request(std::string, std::string body, url_response_callback cb) {

      wait_transaction_params params = fc::json::from_string(body).as<wait_transaction_params>();

      auto condition = parse_condition(params.condition);
      if (params.transaction_id == transaction_id_type()) {
         cb(422, MAKE_ERROR_RESULT(422, "invalid transaction_id"));
      } else if (condition == trx_condition_t::INVALID) {
         cb(422, MAKE_ERROR_RESULT(422, "condition must be 'accepted' or 'finalized'"));
      } else {
         on_wait_request(params.transaction_id, condition, params.timeout, cb);
      }
   }

   void on_block(trx_condition_t status, const block_state_ptr& block_state) {
      for (auto& receipt : block_state->block->transactions) {
         if (!receipt.trx.contains<chain::packed_transaction>())
            continue;

         set_tracked_transaction(status, block_state->block->block_num(),
                                 receipt.trx.get<chain::packed_transaction>().get_transaction());
      }
   }

   void on_accepted_block(const block_state_ptr& block_state) {
      if (lib_slot == 0)
         return; // ignore every accepted blocks before receiving first irreversible_block
                 // because we need lib_slot for calculate expiration slot.
      on_block(trx_condition_t::ACCEPTED, block_state);
   }

   void on_irreversible_block(const block_state_ptr& block_state) {
      if (lib_slot == 0) {
         auto  start_slot = block_state->header.timestamp.slot;
         auto& tid_index  = this->tracked_transactions.get<0>();
         for (auto itr = tid_index.begin(); itr != tid_index.end(); ++itr) {
            tid_index.modify(itr, [start_slot](auto& v) { v.expiration_slot += start_slot; });
         }
      }

      lib_slot = block_state->header.timestamp.slot;
      on_block(trx_condition_t::FINALIZED, block_state);
      clear_expired(lib_slot);
   }

   void clear_expired(uint32_t lib_slot) {
      auto& expiration_index = this->tracked_transactions.get<1>();
      auto  not_expired_pos  = expiration_index.upper_bound(lib_slot);
      for (auto itr = expiration_index.begin(); itr != not_expired_pos; ++itr)
         itr->on_expired();
      expiration_index.erase(expiration_index.begin(), not_expired_pos);
   }

   bool contain_transaction(const transaction_id_type& id) {
      auto& tid_index = this->tracked_transactions.get<0>();
      return tid_index.find(id) != tid_index.end();
   }

   uint32_t get_transaction_expiration_slot(const transaction_id_type& id) {
      auto& tid_index = this->tracked_transactions.get<0>();
      auto  itr       = tid_index.find(id);
      if (tid_index.find(id) != tid_index.end()) {
         return itr->expiration_slot;
      }
      return 0;
   }

   uint32_t current_lib_slot() const { return lib_slot; }
};

//! Transaction tracker to track all transactions observed by a node.

//! This tracker would monitor all transactions observed by the \c accepted_block_channel and \c
//! irreversible_block_channel. When an wait request on a transaction which has not been seen from \c
//! accepted_block_channel and \c irreversible_block_channel, in the specified time window \c num_slots_pass_lib, it
//! would delay the response until the transaction has been seen. If the wait request is sent after the transaction has
//! been seen, it would send the response immediately. A wait request would expire after the time duration specified by
//! \c timeout if no transaction is observed in the time duration and a timeout response would be replied.
class global_transaction_tracker : public transaction_tracker {
 public:
   using transaction_tracker::transaction_tracker;
   void set_tracked_transaction(trx_condition_t status, uint32_t block_num, const transaction& trx) override {
      auto& tid_index      = this->tracked_transactions.get<0>();
      auto [itr, inserted] = tid_index.emplace(trx.id());

      uint32_t expiration_slot = lib_slot + num_slots_pass_lib;

      tid_index.modify(itr, [status, block_num, &trx, expiration_slot](tracked_transaction_state& tracked) {
         if (status == trx_condition_t::FINALIZED || !tracked.wait_cb) {
            // Do not override pending wait expiration slot when accepted
            tracked.expiration_slot = expiration_slot;
         }
         tracked.on_block(status, block_num, trx);
      });
   }

   void on_wait_request(transaction_id_type transaction_id, trx_condition_t condition, uint32_t timeout,
                        url_response_callback cb) override {
      uint32_t expiration_slot = lib_slot + timeout * 2;

      auto& tid_index      = this->tracked_transactions.get<0>();
      auto [itr, inserted] = tid_index.emplace(transaction_id);

      tid_index.modify(itr, [condition, expiration_slot, cb](tracked_transaction_state& tracked) {
         if (tracked.expiration_slot == 0U)
            tracked.expiration_slot = expiration_slot;
         tracked.on_wait_request(condition, cb);
      });
   }
};

//! Transaction tracker to track the transactions sending through a node.

//! This tracker is used to track the transactions sent through a node. The \c num_slots_pass_lib is the difference
//! of the duration from a transaction is sent to it is removed from the memory so it would no longer be trackable.
//! During the time a transaction is trackable, a wait response would be sent when the transaction detail is available.
//! A "404 Not Found" response would be sent when the wait request does not arrive the time windows when the transactin
//! is trackable.
//!
//! Notice that the \c timeout parameter for the wait request has no effect on this tracker.

class local_transaction_tracker : public transaction_tracker {
 public:
   using transaction_tracker::transaction_tracker;
   void add(const transaction_id_type& id) override {
      auto& tid_index = this->tracked_transactions.get<0>();
      tid_index.emplace(id, this->lib_slot + num_slots_pass_lib);
   }

   void set_tracked_transaction(trx_condition_t status, uint32_t block_num, const transaction& trx) override {
      auto& tid_index = this->tracked_transactions.get<0>();
      auto  itr       = tid_index.find(trx.id());

      if (itr == tid_index.end()) {
         return;
      }

      tid_index.modify(itr, [status, block_num, &trx](tracked_transaction_state& tracked) {
         tracked.on_block(status, block_num, trx);
      });
   }

   void on_wait_request(transaction_id_type transaction_id, trx_condition_t condition, uint32_t timeout,
                        url_response_callback cb) override {
      auto& tid_index = this->tracked_transactions.get<0>();
      auto  itr       = tid_index.find(transaction_id);

      if (itr == tid_index.end()) {
         cb(404, MAKE_ERROR_RESULT(404, "the specified transaction is not currently tracked"));
         return;
      }

      tid_index.modify(itr, [condition, &cb](auto& tracked) { tracked.on_wait_request(condition, cb); });
   }
};

} // namespace eosio