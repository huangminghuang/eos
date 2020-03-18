#include <sstream>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/mpl/list.hpp>
#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <snapshots.hpp>

using namespace eosio;
using namespace testing;
using namespace chain;

void remove_existing_blocks(controller::config& config) {
   auto block_log_path = config.blocks_dir / "blocks.log";
   remove(block_log_path);
   auto block_index_path = config.blocks_dir / "blocks.index";
   remove(block_index_path);
}

BOOST_AUTO_TEST_SUITE(restart_chain_tests)

BOOST_AUTO_TEST_CASE(test_existing_state_without_block_log)
{
   tester chain;

   std::vector<signed_block_ptr> blocks;
   blocks.push_back(chain.produce_block());
   blocks.push_back(chain.produce_block());
   blocks.push_back(chain.produce_block());

   tester other;
   for (auto new_block : blocks) {
      other.push_block(new_block);
   }
   blocks.clear();

   other.close();
   auto cfg = other.get_config();
   remove_existing_blocks(cfg);
   // restarting chain with no block log and no genesis
   other.open();

   blocks.push_back(chain.produce_block());
   blocks.push_back(chain.produce_block());
   blocks.push_back(chain.produce_block());
   chain.control->abort_block();

   for (auto new_block : blocks) {
      other.push_block(new_block);
   }
}

BOOST_AUTO_TEST_CASE(test_restart_with_different_chain_id)
{
   tester chain;

   std::vector<signed_block_ptr> blocks;
   blocks.push_back(chain.produce_block());
   blocks.push_back(chain.produce_block());
   blocks.push_back(chain.produce_block());

   tester other;
   for (auto new_block : blocks) {
      other.push_block(new_block);
   }
   blocks.clear();

   other.close();
   genesis_state genesis;
   genesis.initial_timestamp = fc::time_point::from_iso_string("2020-01-01T00:00:01.000");
   genesis.initial_key = eosio::testing::base_tester::get_public_key( config::system_account_name, "active" );
   fc::optional<chain_id_type> chain_id = genesis.compute_chain_id();
   BOOST_REQUIRE_EXCEPTION(other.open(chain_id), chain_id_type_exception, fc_exception_message_starts_with("chain ID in state "));
}

BOOST_AUTO_TEST_CASE(test_restart_with_from_block_log)
{
   tester chain;

   chain.create_account(N(replay1));
   chain.produce_blocks(1);
   chain.create_account(N(replay2));
   chain.produce_blocks(1);
   chain.create_account(N(replay3));
   chain.produce_blocks(1);

   BOOST_REQUIRE_NO_THROW(chain.control->get_account(N(replay1)));
   BOOST_REQUIRE_NO_THROW(chain.control->get_account(N(replay2)));
   BOOST_REQUIRE_NO_THROW(chain.control->get_account(N(replay3)));

   chain.close();

   controller::config copied_config = chain.get_config();
   auto genesis = chain::block_log::extract_genesis_state(chain.get_config().blocks_dir);
   BOOST_REQUIRE(genesis);

   // remove the state files to make sure we are starting from block log
   auto   state_path = copied_config.state_dir;
   remove_all(state_path);
   fc::create_directories(state_path);

   tester from_block_log_chain(copied_config, *genesis);

   BOOST_REQUIRE_NO_THROW(from_block_log_chain.control->get_account(N(replay1)));
   BOOST_REQUIRE_NO_THROW(from_block_log_chain.control->get_account(N(replay2)));
   BOOST_REQUIRE_NO_THROW(from_block_log_chain.control->get_account(N(replay3)));

}

BOOST_AUTO_TEST_SUITE_END()
