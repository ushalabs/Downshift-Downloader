#pragma once
#include "torrent/TorrentEngine.h"
class QWidget;
namespace downshift {
QString torrentStateText(const torrent::Snapshot& snapshot);
bool torrentActive(const torrent::Snapshot& snapshot);
void showAddTorrent(torrent::TorrentEngine& engine,QString folder,QWidget* parent,QString source={});
QWidget* createTorrentDetails(torrent::TorrentEngine& engine,QString id,QWidget* parent);
void showTorrentSettings(torrent::TorrentEngine& engine,QWidget* parent);
}
