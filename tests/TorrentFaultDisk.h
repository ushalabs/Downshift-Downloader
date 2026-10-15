#pragma once
#include <libtorrent/session_params.hpp>
namespace downshift::torrent::testing {
libtorrent::disk_io_constructor_type faultDisk();
void diskBudget(long long bytes);
}
