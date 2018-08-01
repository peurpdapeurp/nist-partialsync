/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2018,  The University of Memphis,
 *                           Regents of the University of California,
 *                           Arizona Board of Regents.
 *
 * This file is part of NLSR (Named-data Link State Routing).
 * See AUTHORS.md for complete list of NLSR authors and contributors.
 *
 * NLSR is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NLSR is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NLSR, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#ifndef PSYNC_LOGGING_HPP
#define PSYNC_LOGGING_HPP

#include <ndn-cxx/util/logger.hpp>

namespace ndn {
namespace psync {

#define _LOG_INIT(name) NDN_LOG_INIT(psync.name)

#define _LOG_FATAL(x) NDN_LOG_FATAL(x)

#define _LOG_ERROR(x) NDN_LOG_ERROR(x)

#define _LOG_WARN(x) NDN_LOG_WARN(x)

#define _LOG_INFO(x) NDN_LOG_INFO(x)

#define _LOG_DEBUG(x) NDN_LOG_DEBUG(x)

#define _LOG_TRACE(x) NDN_LOG_TRACE(x)

} // namespace psync
} // namespace ndn

#endif // PSYNC_LOGGING_HPP
