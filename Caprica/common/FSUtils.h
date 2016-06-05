#pragma once

#include <string>

#include <boost/utility/string_ref.hpp>

#include <common/CaselessStringComparer.h>

namespace caprica { namespace FSUtils {

boost::string_ref basenameAsRef(boost::string_ref file);
boost::string_ref extensionAsRef(boost::string_ref file);
boost::string_ref filenameAsRef(boost::string_ref file);
boost::string_ref parentPathAsRef(boost::string_ref file);

void async_write(const std::string& filename, std::string&& value);
std::string canonical(const std::string& path);

}}
