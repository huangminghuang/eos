#!/usr/bin/env python3

from testUtils import Account
from testUtils import Utils
from Cluster import Cluster
from WalletMgr import WalletMgr
from Node import Node
from Node import ReturnType
from TestHelper import TestHelper
from TestHelper import AppArgs

import decimal
import re


def transfer(node, source, destination, amount):
    try:
        cmd="%s %s -v transfer -j %s %s %s test -p %s@active" % (
                Utils.EosClientPath, node.eosClientArgs(), source.name, destination.name, amountStr, source.name)
        trans = Utils.runCmdArrReturnJson(cmd.split())
        return Node.getTransId(trans)
    except:
        msg=ex.output.decode("utf-8")
        Utils.Print("ERROR: Exception during funds transfer.  %s" % (msg))
        Utils.Print("Failed to transfer \"%s\" from %s to %s" % (amount, source, destination))
        raise


def wait_transaction(node, transaction_id, condition):
    try:
        cmd="%s %s -v wait_transaction -j %s  %s" % (
            Utils.EosClientPath, node.eosClientArgs(), transaction_id, condition)
        Utils.runCmdArrReturnJson(cmd.split())
    except:
        msg=ex.output.decode("utf-8")
        Utils.Print("ERROR: Exception during wait transaction.  %s" % (msg))
        raise

###############################################################
# nodeos_run_test
#
# General test that tests a wide range of general use actions around nodeos and keosd
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit
cmdError=Utils.cmdError
from core_symbol import CORE_SYMBOL

appArgs=AppArgs()
extraArgs = appArgs.add(flag="--transaction-tracker-mode", type=str, help="Most be global or local", default="global")
extraArgs = appArgs.add(flag="--transaction-track-timeout", type=int, help="transaction track timeout in seconds", default=600)
args = TestHelper.parse_args({"--host","--port"
                              ,"--dump-error-details","--keep-logs","-v","--leave-running","--clean-run"
                              ,"--wallet-port","-p"}, applicationSpecificArgs=appArgs)
server=args.host
port=args.port
debug=args.v
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
pnodes=args.p if args.p > 0 else 1
killAll=args.clean_run
walletPort = args.wallet_port
tracker_mode = args.transaction_tracker_mode
track_timeout = args.transaction_track_timeout


Utils.Debug=debug
cluster=Cluster(host=server, port=port, walletd=True)
walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
killEosInstances=not dontKill
killWallet=not dontKill

WalletdName=Utils.EosWalletName
ClientName="cleos"
timeout = .5 * 12 * 2 + 60 # time for finalization with 1 producer + 60 seconds padding
Utils.setIrreversibleTimeout(timeout)

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)
    Print("SERVER: %s" % (server))
    Print("PORT: %d" % (port))

    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, extraNodeosArgs=" --transaction-tracker-mode {} --transaction-track-timeout {}".format(tracker_mode, track_timeout)) is False:
        cmdError("launcher")
        errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)


    testWalletName="test"
    Print("Creating wallet \"%s\"." % (testWalletName))
    walletAccounts=[cluster.eosioAccount, cluster.defproduceraAccount,cluster.defproducerbAccount]
    testWallet=walletMgr.create(testWalletName, walletAccounts)
    Print("Wallet \"%s\" password=%s." % (testWalletName, testWallet.password.encode("utf-8")))

    node = cluster.getNode(0)

    transferAmount="1.0000 {0}".format(CORE_SYMBOL)
    tid = transfer(node, cluster.defproduceraAccount, cluster.defproducerbAccount, transferAmount)
    wait_transaction(tid, "accepted")
    wait_transaction(tid, "finalized")

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, killEosInstances, killWallet, keepLogs, killAll, dumpErrorDetails)

exit(0)
