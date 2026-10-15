#include "TorrentFaultDisk.h"
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/peer_request.hpp>
#include <libtorrent/error_code.hpp>
#include <boost/asio/post.hpp>
#include <atomic>
namespace downshift::torrent::testing {
namespace lt=libtorrent;
namespace {
std::atomic<long long> budget{-1};
class FaultDisk final:public lt::disk_interface {
    std::unique_ptr<lt::disk_interface> disk;lt::io_context& io;
public:
    FaultDisk(lt::io_context& context,lt::settings_interface const& settings,lt::counters& counters):disk(lt::default_disk_io_constructor(context,settings,counters)),io(context){}
    lt::storage_holder new_torrent(lt::storage_params const& p,std::shared_ptr<void> const& t)override{return disk->new_torrent(p,t);}
    void remove_torrent(lt::storage_index_t s)override{disk->remove_torrent(s);}
    void async_read(lt::storage_index_t s,lt::peer_request const& r,std::function<void(lt::disk_buffer_holder,lt::storage_error const&)> h,lt::disk_job_flags_t f)override{disk->async_read(s,r,std::move(h),f);}
    bool async_write(lt::storage_index_t s,lt::peer_request const& r,char const* b,std::shared_ptr<lt::disk_observer> o,std::function<void(lt::storage_error const&)> h,lt::disk_job_flags_t f)override{
        if(budget.load()>=0&&budget.fetch_sub(r.length)<r.length){budget=0;boost::asio::post(io,[h=std::move(h)]{h(lt::storage_error(make_error_code(boost::system::errc::no_space_on_device),lt::operation_t::file_write));});return false;}return disk->async_write(s,r,b,std::move(o),std::move(h),f);
    }
    void async_hash(lt::storage_index_t s,lt::piece_index_t p,lt::span<lt::sha256_hash> v,lt::disk_job_flags_t f,std::function<void(lt::piece_index_t,lt::sha1_hash const&,lt::storage_error const&)> h)override{disk->async_hash(s,p,v,f,std::move(h));}
    void async_hash2(lt::storage_index_t s,lt::piece_index_t p,int o,lt::disk_job_flags_t f,std::function<void(lt::piece_index_t,lt::sha256_hash const&,lt::storage_error const&)> h)override{disk->async_hash2(s,p,o,f,std::move(h));}
    void async_move_storage(lt::storage_index_t s,std::string p,lt::move_flags_t f,std::function<void(lt::status_t,std::string const&,lt::storage_error const&)> h)override{disk->async_move_storage(s,std::move(p),f,std::move(h));}
    void async_release_files(lt::storage_index_t s,std::function<void()> h)override{disk->async_release_files(s,std::move(h));}
    void async_check_files(lt::storage_index_t s,lt::add_torrent_params const* p,lt::aux::vector<std::string,lt::file_index_t> l,std::function<void(lt::status_t,lt::storage_error const&)> h)override{disk->async_check_files(s,p,std::move(l),std::move(h));}
    void async_stop_torrent(lt::storage_index_t s,std::function<void()> h)override{disk->async_stop_torrent(s,std::move(h));}
    void async_rename_file(lt::storage_index_t s,lt::file_index_t i,std::string n,std::function<void(std::string const&,lt::file_index_t,lt::storage_error const&)> h)override{disk->async_rename_file(s,i,std::move(n),std::move(h));}
    void async_delete_files(lt::storage_index_t s,lt::remove_flags_t o,std::function<void(lt::storage_error const&)> h)override{disk->async_delete_files(s,o,std::move(h));}
    void async_set_file_priority(lt::storage_index_t s,lt::aux::vector<lt::download_priority_t,lt::file_index_t> p,std::function<void(lt::storage_error const&,lt::aux::vector<lt::download_priority_t,lt::file_index_t>)> h)override{disk->async_set_file_priority(s,std::move(p),std::move(h));}
    void async_clear_piece(lt::storage_index_t s,lt::piece_index_t p,std::function<void(lt::piece_index_t)> h)override{disk->async_clear_piece(s,p,std::move(h));}
    void update_stats_counters(lt::counters& c)const override{disk->update_stats_counters(c);}
    std::vector<lt::open_file_state> get_status(lt::storage_index_t s)const override{return disk->get_status(s);}
    void abort(bool w)override{disk->abort(w);}
    void submit_jobs()override{disk->submit_jobs();}
    void settings_updated()override{disk->settings_updated();}
};
}
void diskBudget(long long bytes){budget=bytes;}
lt::disk_io_constructor_type faultDisk(){return [](lt::io_context& i,lt::settings_interface const& s,lt::counters& c){return std::make_unique<FaultDisk>(i,s,c);};}
}
