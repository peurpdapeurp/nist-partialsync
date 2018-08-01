# -*- Mode:python; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
#
# Copyright (C) 2015-2018, The University of Memphis,
#                          Arizona Board of Regents,
#                          Regents of the University of California.
#
# This file is part of Mini-NDN.
# See AUTHORS.md for a complete list of Mini-NDN authors and contributors.
#
# Mini-NDN is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Mini-NDN is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Mini-NDN, e.g., in COPYING.md file.
# If not, see <http://www.gnu.org/licenses/>.

from ndn.experiments.experiment import Experiment

import time

class PsyncFullExperiment(Experiment):

    def __init__(self, args):

        Experiment.__init__(self, args)
        self.syncPrefix = "/sync/psync"
        self.logDebug = self.arguments.logDebug

    @staticmethod
    def parseArguments(parser):
        parser.add_argument("--logDebug", dest="logDebug", action="store_true",
                            help="[PSync Full Experiment] Enable logging")

    def setup(self):
        pass

    def run(self):
        for host in self.net.hosts:
            host.nfd.setStrategy(self.syncPrefix, "multicast")
            self.registerRouteToAllNeighbors(host)

        time.sleep(15)

        #for host in self.net.hosts:
        #    for intf in host.intfNames():
        #        ndnDumpOutputFile = "dump.{}".format(intf)
        #        host.cmd("sudo ndndump -i {} -f \'.*/sync/psync/full.*\' > {} &".format(intf, ndnDumpOutputFile))

        for host in self.net.hosts:
            if self.logDebug:
                host.cmd("export NDN_LOG=psync.*=DEBUG")
            else:
                host.cmd("export NDN_LOG=psync.*=INFO")
            host.cmd("ASAN_OPTIONS=\"color=always\"")
            host.cmd("ASAN_OPTIONS+=\":detect_stack_use_after_return=true\"")
            host.cmd("ASAN_OPTIONS+=\":check_initialization_order=true\"")
            host.cmd("ASAN_OPTIONS+=\":strict_init_order=true\"")
            host.cmd("ASAN_OPTIONS+=\":detect_invalid_pointer_pairs=1\"")
            host.cmd("ASAN_OPTIONS+=\":detect_container_overflow=false\"")
            host.cmd("ASAN_OPTIONS+=\":strict_string_checks=true\"")
            host.cmd("ASAN_OPTIONS+=\":strip_path_prefix=${PWD}/\"")
            host.cmd("export ASAN_OPTIONS")
            host.cmd("psync-full /sync/psync /test/{} 5 &> psync.logs &".format(host.name))

    def registerRouteToAllNeighbors(self, host):
        for intf in host.intfList():
            link = intf.link
            if link:
                node1, node2 = link.intf1.node, link.intf2.node

                if node1 == host:
                    other = node2
                    ip = other.IP(str(link.intf2))
                else:
                    other = node1
                    ip = other.IP(str(link.intf1))
            host.cmd("nfdc face create udp://{}".format(ip))
            host.cmd("nfdc route add {} udp://{}".format(self.syncPrefix, ip))

Experiment.register("psync-full", PsyncFullExperiment)

# Example:
# sudo minindn --exp psync-full ndn_utils/topologies/minindn.caida.conf --logDebug --no-nlsr
