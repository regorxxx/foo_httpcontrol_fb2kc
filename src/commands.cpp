#include "stdafx.h"
#include "httpserver.h"
#include "facets.h"
#include "browsefiles.h"
extern "C" {
#include <Powrprof.h>
}
#pragma comment(lib, "Powrprof.lib")

namespace httpc
{
namespace control
{
	commands_map commands;

	extern pfc::string8 command_result;

	class process_locations_notify_my : public process_locations_notify { // callback from enqueue call
	public:
		void on_completion(const pfc::list_base_const_t<metadb_handle_ptr> & p_items)
		{
			static_api_ptr_t<playlist_manager> plm;

			if (httpc::registered_extensions.get_count() > 0)
			{
				pfc::list_t<metadb_handle_ptr> p_items_toadd;

				t_size l = p_items.get_count();

				for (size_t i = 0; i < l; ++i)
				{
					const char *item_path = p_items[i]->get_path();

					if (httpc::is_extension_registered(item_path) || httpc::is_protocol_allowed(item_path))
						p_items_toadd.add_item(p_items[i]);
					else
						foo_error(pfc::string8() << "skipping \"" << item_path << "\": unrecognized extension or not allowed protocol");
				}

				if (!plm->activeplaylist_add_items(p_items_toadd, bit_array_true()))
					activeplaylist_add_items_fail_msg();
			}
			else
			{
				if (!plm->activeplaylist_add_items(p_items, bit_array_true()))
					activeplaylist_add_items_fail_msg();
			}

			on_aborted();
		};

		void on_aborted() { httpc::enqueueing = false; };

		void activeplaylist_add_items_fail_msg()
		{
			foo_error("activeplaylist_add_items failed: active playlist must exist and be writeable");
		}
	};

	bool cmd_stop(foo_httpserver_command *cmd)
	{
		httpc::pb_time = 0;
		static_api_ptr_t<playback_control>()->stop();

		return true;
	}

	bool cmd_playorpause(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playback_control> pbc;

		pbc->play_or_pause();

		return true;
	}

	bool cmd_start(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		if (param1.length())
		{
			static_api_ptr_t<playlist_manager>()->activeplaylist_execute_default_action(atoi(param1));

			httpc::last_action = httpc::FLC_START;
			httpc::should_update_queue = true;
			should_focus_on_playing = true;
		}
		else
		{
			static_api_ptr_t<playback_control> pbc;

			if (pbc->is_playing() && !pbc->is_paused() && pbc->playback_can_seek())
				pbc->playback_seek(0);
			else
			if (pbc->is_paused() || !pbc->is_paused()&&!pbc->is_playing())
				pbc->play_or_pause();
		}

		return true;
	}

	bool cmd_setfocus(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		static_api_ptr_t<playlist_manager>()->activeplaylist_set_focus_item(atoi(param1));
		httpc::playlist_item_focused = static_api_ptr_t<playlist_manager>()->activeplaylist_get_focus_item();

		httpc::state_changed |= FSC_PLAYLIST;

		httpc::last_action = httpc::FLC_FOCUS;

		return true;
	}

	bool cmd_startrandom(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playback_control>()->start(playback_control::track_command_rand, false);

		return true;
	}

	bool cmd_startprevious(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control> pc;

		httpc::pb_time = 0;

		pc->start(playback_control::track_command_prev, false);

		return true;
	}

	bool cmd_startnext(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control> pc;

		httpc::pb_time = 0;
		httpc::update_previouslyplayed();
		pc->start(playback_control::track_command_next, false);

		return true;
	}

	bool cmd_volume(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		httpc::set_volume(atoi(param1));

		return true;
	}

	bool cmd_volume_delta(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		int vol = httpc::get_volume() + atoi(param1);

		if (vol < 0)
			vol = 0;

		if (vol > 100)
			vol = 100;

		httpc::set_volume(vol);

		return true;
	}

	bool cmd_volume_db(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		httpc::volume = (float)(atoi(param1) / -10.0);

		static_api_ptr_t<play_control>()->set_volume(httpc::volume);

		return true;
	}

	bool cmd_volume_db_delta(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control> pc;

		pfc::string8 &param1 = cmd->get_param(0);

		float delta = (float)(atoi(param1) / 10.0);
		float volume = pc->get_volume();

		if (volume + 100.0 < 0.1)
			volume = -66.5;

		volume += delta;

		if (volume < -100)
			volume = -100;

		if (volume > -100 && volume < -66.5)
			volume = -100;

		pc->set_volume(volume);
		httpc::volume = pc->get_volume();

		return true;
	}

	bool cmd_volume_mute_toggle(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control>()->volume_mute_toggle();

		return true;
	}

	bool cmd_seekpercent(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control> pc;

		if (pc->playback_can_seek())
		{
			pfc::string8 param1 = cmd->get_param(0);

			double new_pos = httpc::pb_length * (atoi(param1) / 100.0);
			httpc::pb_time = new_pos;
			pc->playback_seek(new_pos);
			return true;
		}
		else
			return false;
	}

	bool cmd_seekdelta(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control> pc;

		if (pc->playback_can_seek())
		{
			pfc::string8 &param1 = cmd->get_param(0);

			pc->playback_seek_delta(atoi(param1)*1.0);

			return true;
		}
		else
			return false;
	}

	bool cmd_seeksecond(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<play_control> pc;

		if (pc->playback_can_seek())
		{
			pfc::string8 param1 = cmd->get_param(0);

			double new_pos = atoi(param1);
			httpc::pb_time = new_pos;
			pc->playback_seek(new_pos);
			return true;
		}
		else
			return false;
	}

	bool cmd_emptyplaylist(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		t_size playlist = ~0;

		if (param1 == "")
			playlist = plm->get_active_playlist();
		else
			playlist = atoi(param1);

		plm->playlist_undo_backup(playlist);
		plm->playlist_clear(playlist);

		httpc::playlist_page = 1;
		httpc::refresh_playing_info();

		return true;
	}

	bool cmd_undo(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		t_size playlist = ~0;

		if (param1 == "")
			playlist = plm->get_active_playlist();
		else
			playlist = atoi(param1);

		bool result = plm->playlist_undo_restore(playlist);
		httpc::refresh_playing_info();

		return result;
	}

	bool cmd_redo(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		t_size playlist = ~0;

		if (param1 == "")
			playlist = plm->get_active_playlist();
		else
			playlist = atoi(param1);

		bool result = plm->playlist_redo_restore(playlist);
		httpc::refresh_playing_info();

		return result;
	}

	bool cmd_playbackorder(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		if (static_cast<t_size>(atoi(param1)) < plm->playback_order_get_count())
		{
			plm->playback_order_set_active(atoi(param1));
			return true;
		}
		else
			return false;
	}

	bool cmd_playlistitemsperpage(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);
		t_size n = atoi(param1);

		tcfg.get().playlist_items_per_page = n > tcfg.get().playlist_items_per_page_max ? tcfg.get().playlist_items_per_page_max : n;

		should_update_playlist = true;
		httpc::should_focus_on_playing = true;
		return true;
	}

	bool cmd_del(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 param1 = cmd->get_param(0);
		pfc::string8 param2 = cmd->get_param(1);

		t_size playlist = ~0;

		if (param2 == "")
			playlist = plm->get_active_playlist();
		else
			playlist = atoi(param2);

		plm->playlist_undo_backup(playlist);

		bit_array_bittable items(0);
		str_to_bitarray(param1, items);

		plm->playlist_remove_items(playlist, items);

		httpc::last_action = httpc::FLC_REMOVE;

		return true;
	}

	bool cmd_move(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);
		pfc::string8 &param2 = cmd->get_param(1);

		bit_array_bittable items(0);
		str_to_bitarray(param1, items);

		plm->activeplaylist_undo_backup();

		plm->activeplaylist_set_selection(bit_array_true(), bit_array_false());
		plm->activeplaylist_set_selection(items, bit_array_true());
		plm->activeplaylist_move_selection(atoi(param2));

		httpc::last_action = httpc::FLC_SHIFT; // todo: wtf is this?
		httpc::refresh_playing_info();

		return true;
	}

	bool cmd_switchplaylist(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);
		pfc::string8 &param2 = cmd->get_param(1);

		plm->set_active_playlist(atoi(param1));
		httpc::playlist_page = 1;
		httpc::should_focus_on_playing = true;

		return true;
	}

	bool walk_contextmenu(contextmenu_node *node, pfc::string8 &command, pfc::string8 path)
	{
		if (node && pfc::strlen_max(node->get_name(), ~0) != 0)
		{
			if (path.get_length())
				path << "/";

			path << node->get_name();

			if (pfc::stricmp_ascii(path, command) == 0)
			{
				foo_info(pfc::string_formatter() << "executing " << path);

				node->execute();

				return true;
			}
			else
			if (pfc::string_find_first(command, path, 0) == 0)
			{
				t_size l = node->get_num_children();

				for (t_size i = 0; i < l; ++i)
					if (walk_contextmenu(node->get_child(i), command, path))
						return true;
			}
		}

		return false;
	}

	bool cmd_playingcommand(foo_httpserver_command *cmd)
	{
		pfc::string8 param1 = cmd->get_param(0);

		if (param1.get_length())
		{
			static_api_ptr_t<playlist_manager> plm;
			static_api_ptr_t<contextmenu_manager> cmm;

			if (cmm->init_context_now_playing(contextmenu_manager::flag_view_full))
			{
				contextmenu_node *node = cmm->get_root();

				param1 = pfc::string_formatter() << "Root/" << param1;

				plm->playlist_undo_backup(plm->get_playing_playlist());

				if (walk_contextmenu(node, param1, ""))
				{
					httpc::should_update_playlist = true;

					return true;
				}
			}
		}

		return false;
	}

	bool cmd_selectioncommand(foo_httpserver_command *cmd)
	{
		pfc::string8 param1 = cmd->get_param(0);
		pfc::string8 param2 = cmd->get_param(1);

		if (param1.get_length())
		{
			static_api_ptr_t<playlist_manager> plm;
			static_api_ptr_t<contextmenu_manager> cmm;

			if (param2.get_length())
			{
				bit_array_bittable items(0);
				str_to_bitarray(param2, items);

				plm->activeplaylist_set_selection(bit_array_true(), bit_array_false());
				plm->activeplaylist_set_selection(items, bit_array_true());
			}

			pfc::list_t<metadb_handle_ptr> items;
			plm->activeplaylist_get_selected_items(items);

			if (items.get_count())
			{
				cmm->init_context(items, contextmenu_manager::flag_view_full);

				contextmenu_node *node = cmm->get_root();

				param1 = pfc::string_formatter() << "Root/" << param1;

				plm->activeplaylist_undo_backup();

				if (walk_contextmenu(node, param1, ""))
				{
					httpc::should_update_playlist = true;

					return true;
				}
			}
		}

		return false;
	}

	bool cmd_setselection(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);
		pfc::string8 &param2 = cmd->get_param(1);

		t_size playlist = plm->get_active_playlist();

		if (param2.get_length())
			playlist = atoi(param2);

		bit_array_bittable items(0);
		str_to_bitarray(param1, items);

		if (param1.get_length() == 0) // removing all selection
			plm->playlist_set_selection(playlist, bit_array_true(), bit_array_false());
		else
		if (param1.get_length() == 1 && param1.find_first('~') == 0) // selecting everything
			plm->playlist_set_selection(playlist, bit_array_true(), bit_array_true());
		else // selecting specified items
			plm->playlist_set_selection(playlist, items, bit_array_true());

		return true;
	}

	bool cmd_queueitems(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		pfc::list_t<t_size> list;
		str_to_list(param1, list);
		httpc::enqueue(list);
		httpc::last_action = httpc::FLC_ENQUEUE;

		return true;
	}

	bool cmd_dequeueitems(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		pfc::list_t<t_size> list;
		str_to_list(param1, list);
		httpc::dequeue(list);
		httpc::last_action = httpc::FLC_DEQUEUE;

		return true;
	}

	bool cmd_saq(foo_httpserver_command *cmd)
	{
		if (cfg.main.stop_after_queue_enable)
		{
			pfc::string8 &param1 = cmd->get_param(0);

			cfg.misc.stop_after_queue = atoi(param1) == 1 ? true : false;
		}

		return true;
	}

	bool cmd_sac(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playback_control> pc;
		pfc::string8 &param1 = cmd->get_param(0);

		if (atoi(param1) == 1)
			pc->set_stop_after_current(true);
		else
			pc->set_stop_after_current(false);

		httpc::sac = pc->get_stop_after_current();

		return true;
	}

	bool cmd_flushqueue(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;

		plm->queue_flush();

		return true;
	}

	bool cmd_queuealbum(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playback_control> pc;
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		t_size focus;

		if (param1.get_length() == 0)
			focus = plm->activeplaylist_get_focus_item();
		else
			focus = atoi(param1);

		if (focus != ~0
			&& focus < plm->activeplaylist_get_item_count()
			&& plm->activeplaylist_get_item_count() > 0
			&& plm->queue_get_count() < 64)
		{
			service_ptr_t<titleformat_object> album_script;
			static_api_ptr_t<titleformat_compiler>()->compile_safe(album_script,"%ARTIST% - %ALBUM% - %DATE%");

			pfc::string8 first, next;

			t_size count = 0;

			if (plm->activeplaylist_get_item_handle(focus)->format_title(NULL, first, album_script, NULL))
			{
				t_size len = plm->activeplaylist_get_item_count();
				for (t_size idx = focus; idx < len && plm->queue_get_count() < 64; ++idx)
				{
					next.reset();

					if (plm->activeplaylist_get_item_handle(idx)->format_title(NULL, next, album_script, NULL) && next == first)
					{
						plm->queue_add_item_playlist(plm->get_active_playlist(), idx);
						++count;
					}
					else
						break;
				}
			}
			album_script.release();

			return true;
		}
		else
			return false;
	}

	// todo: improve efficiency
	bool cmd_queuerandomitems(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playback_control> pc;
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 &param1 = cmd->get_param(0);

		t_size items_to_be_queued = atoi(param1);
		t_size items_queued_in_active_playlist = 0;
		t_size items_available_in_active_playlist = 0;
		t_size active_playlist = plm->get_active_playlist();

		pfc::list_t<metadb_handle_ptr> playlist_items;
		pfc::list_t<t_playback_queue_item> queue;
		plm->queue_get_contents(queue);

		bit_array_bittable mask(plm->activeplaylist_get_item_count());

		t_size l = queue.get_count();
		for (t_size k = 0; k < l; ++k)
			if (queue[k].m_playlist == active_playlist
				&& !mask.get(queue[k].m_item))
			{
				mask.set(queue[k].m_item, true);
				++items_queued_in_active_playlist;
			}


		plm->activeplaylist_get_all_items(playlist_items);

		playlist_items.remove_mask(mask);

		items_available_in_active_playlist = plm->activeplaylist_get_item_count() - items_queued_in_active_playlist;

		if (items_to_be_queued == 0)
			items_to_be_queued = 1;

		if (items_to_be_queued > items_available_in_active_playlist)
			items_to_be_queued = items_available_in_active_playlist;

		if (items_to_be_queued > 64)
			items_to_be_queued = 64;

		srand((unsigned int)__rdtsc());
		for (;items_to_be_queued > 0; --items_to_be_queued)
		{
			t_size p_item_random;
			t_size p_playlist_item;

			p_item_random = rand() % (playlist_items.get_count());

			if (plm->playlist_find_item(active_playlist, playlist_items[p_item_random], p_playlist_item))
			{
				plm->queue_add_item_playlist(active_playlist, p_playlist_item);
				playlist_items.remove_by_idx(p_item_random);
			}
		}

		return true;
	}

	bool cmd_queryadvance(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		if (param1.get_length())
		{
			httpc::facets::next_facet(param1);
			httpc::facets::fill_playlist();
			httpc::should_update_playlist = true;
			httpc::playlist_page = 1;
			return true;
		}
		else
			return false;
	}

	bool cmd_queryretrace(foo_httpserver_command *cmd)
	{
		httpc::facets::prev_facet();
		httpc::facets::fill_playlist();
		httpc::should_update_playlist = true;
		httpc::should_update_playlists_list = true;

		return true;
	}

	bool cmd_searchmedialibrary(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		if (!library_manager::get()->is_library_enabled()
			|| param1.get_length() == 0)
			return false;

		const auto plm = playlist_manager::get();

		auto playlist = query_select_or_create_playlist(cfg.query.sendtodedicated, cfg.misc.query_playlist_name);

		if (playlist != pfc::infinite_size)
		{
			httpc::autoplaylist_request = param1;

			try
			{
				plm->playlist_clear(playlist);

				metadb_handle_list items;
				library_manager::get()->get_all_items(items);
				metadb_handle_list dst_list(items);
				
				search_filter_v2::ptr filter = search_filter_manager_v2::get()->create_ex(param1.toString(),
					fb2k::service_new<completion_notify_dummy>(),
					search_filter_manager_v2::KFlagSuppressNotify);

				pfc::array_t<bool> mask;
				mask.set_size(dst_list.get_count());
				filter->test_multi(dst_list, mask.get_ptr());
				dst_list.filter_mask(mask.get_ptr());

				plm->playlist_insert_items(playlist, 0, dst_list, pfc::bit_array_val(false));
				plm->playlist_sort_by_format(playlist, cfg.query.sortpattern, false);

				httpc::should_update_playlist = true;
				httpc::query_playlist = playlist;
				httpc::should_update_playlists_list = true;
				httpc::playlist_page = 1;
				return true;
			}
			catch (pfc::exception &e)
			{
				foo_error(e.what());
			}
		}

		return false;
	}

	bool cmd_cmdline(foo_httpserver_command *cmd)
	{
		pfc::string8 &param1 = cmd->get_param(0);

		pfc::stringcvt::string_wide_from_utf8 cmd_w (httpc::fb2k_path);
		pfc::stringcvt::string_wide_from_utf8 par_w (param1);

		foo_info(pfc::string_formatter() << "executing " << httpc::fb2k_path << " " << param1);

		if ((t_size)ShellExecute(0, L"open", cmd_w, par_w, 0, SW_NORMAL) <= 32)
			return false;

		httpc::should_update_playlist = true;
		httpc::should_update_playlists_list = true;
		httpc::should_update_queue = true;
		httpc::should_update_playlist_total_time = true;

		return true;
	}

	bool cmd_switchplaylistpage(foo_httpserver_command *cmd)
	{
		if (tcfg.get().playlist_items_per_page == 0)
			return false;

		pfc::string8 &param1 = cmd->get_param(0);
		static_api_ptr_t<playlist_manager> plm;
		t_size page = atoi(param1);

		t_size total_pages = (t_size)ceil(plm->activeplaylist_get_item_count()*1.0 / tcfg.get().playlist_items_per_page);

		if (page > total_pages)
			page = total_pages;

		if (page == 0)
			page = 1;

		httpc::playlist_page = page;
		httpc::should_update_playlist = true;
		httpc::should_update_queue = true;

		return true;
	}

	bool cmd_browse(foo_httpserver_command *cmd)
	{
		if (httpc::enqueueing)
		{
			foo_error("Enqueueing in progress");
			return false;
		}

		pfc::string8 param1 = cmd->get_param(cmd->E_PARAM1);
		pfc::string8 param2 = cmd->get_param(cmd->E_PARAM2);

		pfc::string_list_impl files;        // files/dirs to be enqueued

		bool is_dir = false;
		bool is_file = false;
		bool is_url = false;

		// if no path specified, use last browse directory
		if (param1.get_length() == 0)
			param1 = cfg.misc.last_browse_dir;

		// check if param2 is a valid command
		if (param2.get_length() > 0 &&
				(strcmp(param2, "EnqueueDir") != 0 && strcmp(param2, "EnqueueDirSubdirs") != 0))
			foo_error(pfc::string8() << "ignoring param2: \"" << param2 << "\": unknown mode");

		// assuming path is a directory if ends with separator
		// " " is a root shortcut, also counts as a directory
		if (param1.ends_with('\\') || strcmp(param1, " ") == 0)
			is_dir = true;

		if (httpc::is_protocol_allowed(param1))
			is_url = true;

		if (!is_dir && !is_url)
			is_file = true;

		if (is_dir || is_file)
		{
			if (!httpc::is_path_allowed(param1))
			{
				foo_error(pfc::string8() << "ignoring param1: \"" << param1 << "\": doesn't match Allowed paths");
				param1 = "%20";
				is_dir = True;
			}
		}

		// try to enqueue param1 if is a file with allowed extension, or url with allowed protocol
		if ((is_file && httpc::is_extension_registered(param1) || is_url)
			&& param2.get_length() == 0)  // add a file / location
		{
				files.add_item(param1);
		}
		else
			is_file = false;

		if (!is_dir && !is_file && !is_url)
			foo_error(pfc::string8() << "ignoring param1: \"" << param1 << "\": not a directory, unrecognized extension or not allowed protocol");

		if (is_dir && param2.get_length() != 0)
		{
			if (strcmp(param2, "EnqueueDirSubdirs") == 0) // adding a nested directory
				files.add_item(param1);

			if (strcmp(param2, "EnqueueDir") == 0) // adding a directory without nesting;
			{
				foo_browsefiles browser;
				browser.browse(const_cast<char *>(param1.operator const char *()));

				t_size l = browser.entries.get_count();
				for(unsigned int i = 0; i < l; ++i)
					if (browser.entries[i].type != foo_browsefiles::ET_DIR)
						files.add_item(browser.entries[i].path);
			}
		}

		if (files.get_count())	// if there is anything to enqueue
		{
			static_api_ptr_t<playlist_manager>()->activeplaylist_undo_backup();

			service_ptr_t<process_locations_notify_my> notify = new service_impl_t<process_locations_notify_my>();
			httpc::enqueueing = true;

			static_api_ptr_t<playlist_incoming_item_filter_v2>()->process_locations_async(
				files,
				playlist_incoming_item_filter_v2::op_flag_background | playlist_incoming_item_filter_v2::op_flag_delay_ui,
				httpc::restrict_mask,
				NULL,
				core_api::get_main_window(),
				notify
			);

		}

		return true;
	}

	bool cmd_parse(foo_httpserver_command *cmd)
	{
		return true;
	}

	bool cmd_removeplaylist(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;

		if (cmd->get_param(0))
		{
			t_size playlist = atoi(cmd->get_param(0));
			return plm->remove_playlist_switch(playlist);
		}
		else
			return plm->remove_playlist_switch(plm->get_active_playlist());
	}

	bool cmd_createplaylist(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 param1 = cmd->get_param(0);
		pfc::string8 param2 = cmd->get_param(1);

		t_size pindex;

		if (!param2.get_length() && plm->get_active_playlist() != pfc::infinite_size)
			pindex = plm->get_active_playlist() + 1;
		else
		if (param2.get_length())
			pindex = atoi(param2) + 1;
		else
			pindex = pfc::infinite_size;

		if (param1.get_length())
			plm->create_playlist(param1, pfc::infinite_size, pindex);
		else
			plm->create_playlist_autoname(pindex);

		return true;
	}


	bool cmd_renameplaylist(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 param1 = cmd->get_param(0);
		pfc::string8 param2 = cmd->get_param(1);

		t_size pindex;

		if (param2.get_length())
			pindex = atoi(param2);
		else
			pindex = plm->get_active_playlist();

		if (param1.get_length())
			plm->playlist_rename(pindex, param1, pfc::infinite_size);

		return true;
	}

	bool cmd_refreshplayinginfo(foo_httpserver_command *cmd)
	{
		httpc::refresh_playing_info();

		return true;
	}

	bool cmd_sort_ascending(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		pfc::string8 param1 = cmd->get_param(0);
		pfc::string8 param2 = cmd->get_param(1);

		if (param1.get_length() == 0) // no sort pattern specified
			return false;

		// create an undo point
		plm->activeplaylist_undo_backup();

		if (param2.get_length())
		{
			// select active playlist individual items
			bit_array_bittable items(0);
			str_to_bitarray(param2, items);

			plm->activeplaylist_set_selection(bit_array_true(), bit_array_false());
			plm->activeplaylist_set_selection(items, bit_array_true());
		}
		else
		{
			// select whole active playlist
			plm->activeplaylist_set_selection(bit_array_true(), bit_array_true());
		}

		return plm->activeplaylist_sort_by_format(param1.toString(), true);
	}

	bool cmd_sort_descending(foo_httpserver_command *cmd)
	{
		// sorting
		if (!cmd_sort_ascending(cmd))
			return false;

		// reversing sort result
		static_api_ptr_t<playlist_manager> plm;
		t_size count, selection_count;
		pfc::array_t<t_size> order;

		count = plm->activeplaylist_get_item_count();

		bit_array_bittable table(count);
		order.set_size(count);
		selection_count = plm->activeplaylist_get_selection_count(plm->activeplaylist_get_item_count());

		plm->activeplaylist_get_selection_mask(table);

		if (count == selection_count)
		{
			// whole playlist is selected
			// create a simple reverse order
			for(t_size walk = 0, walk_reverse = count - 1; walk < count; ++walk, --walk_reverse) {
				order[walk_reverse] = walk;
			}
		} else {
			// individual playlist items are selected
			pfc::array_t<t_size> selection;
			selection.set_size(selection_count);

			// create straight order, and fill selection indexes array
			for(t_size walk = 0, s = 0; walk < count; ++walk) {
				order[walk] = walk;
				if (table[walk])
					selection[s++] = walk;
			}

			// modify order with reversed selection indexes array
			for(t_size walk = 0, s = selection_count-1; walk < count; ++walk) {
				if (table[walk])
					order[walk] = selection[s--];
			}
		}

		return plm->activeplaylist_reorder_items(order.get_ptr(), count);
	}

	bool cmd_focusonplaying(foo_httpserver_command *cmd)
	{
		static_api_ptr_t<playlist_manager> plm;
		static_api_ptr_t<playback_control> pc;

		t_size playing_playlist = plm->get_playing_playlist();
		t_size active_playlist = plm->get_active_playlist();
		bool is_playing = pc->is_playing() || pc->is_paused();

		if (playing_playlist != pfc::infinite_size && playing_playlist != active_playlist && is_playing)
			plm->set_active_playlist(playing_playlist);

		if (is_playing)
		{
			httpc::should_focus_on_playing = true;
			httpc::should_update_playlist = true;
		}

		return true;
	}

	bool cmd_formattitles(foo_httpserver_command *cmd)
	{
		pfc::string8 param1 = cmd->get_param(0);	// list of titles separated by |
		t_size param2;								// active playlist item index, omit to use playing item

		if (cmd->get_param(1).get_length() > 0)
			param2 = atoi(cmd->get_param(1));
		else
			param2 = pfc::infinite_size;

		static_api_ptr_t<playback_control> pc;
		static_api_ptr_t<playlist_manager> plm;
		metadb_handle_ptr item_ptr;

		service_ptr_t<titleformat_object> title_script;
		pfc::list_t<pfc::string8> titles;
		pfc::string8 formatted_title;

		pfc::splitStringSimple_toList(titles, '|', param1);

		command_result = "[";

		t_size l = titles.get_count();
		bool first = true;

		for (t_size i = 0; i < l; ++i)
		{
			static_api_ptr_t<titleformat_compiler>()->compile_safe(title_script,titles.get_item(i));

			if (param2 == pfc::infinite_size && (pc->is_playing() || pc->is_paused()))
				pc->playback_format_title(NULL,formatted_title,title_script,NULL,playback_control::display_level_all);
			else
			if (param2 != pfc::infinite_size && plm->activeplaylist_get_item_handle(item_ptr, param2))
				item_ptr->format_title(NULL, formatted_title, title_script, NULL);
			else
				formatted_title.reset();

			if (formatted_title.get_length())
			{
				if (!first)
					command_result << ",";
				else
					first = false;

				command_result << "\"" << xml_friendly_string(formatted_title) << "\"";
			}
		}

		command_result << "]";

		return true;
	}

	bool cmd_version(foo_httpserver_command* cmd)
	{
		auto param1 = cmd->get_param(0);
		//Foobar controller requires specific version to work properly
		command_result = "{\"versionName\":\"";
		command_result << (param1.get_length() == 0 ? VER_CONTROLLER : VER_PRODUCT_VERSION_STR);
		command_result << "\", \"versionCode\":\"1\"}";
		return true;
	}

	bool cmd_foobarversion(foo_httpserver_command* cmd)
	{
		auto param1 = cmd->get_param(0);
		static_api_ptr_t<core_version_info_v2> version;
		command_result = (param1.get_length() == 0 ? "foobar2000 v" : "");
		command_result << version->get_version_as_text();
		return true;
	}

	bool cmd_getqueue(foo_httpserver_command* cmd)
	{
		static_api_ptr_t<playlist_manager> plm;

		pfc::list_t<t_playback_queue_item> queue;
		plm->queue_get_contents(queue);

		service_ptr_t<titleformat_object> track_script;
		static_api_ptr_t<titleformat_compiler>()->compile_safe(
			track_script,
			"$if2(%TITLE%,?)" HTTPC_TF_SEP "$if2(%ARTIST%,?)" HTTPC_TF_SEP "$if2(%ALBUM%,?)" HTTPC_TF_SEP "$if2(%LENGTH%,0:00)" HTTPC_TF_SEP "$max(%RATING%, 0)"
		);
		pfc::string8 track_info;
		pfc::list_t<pfc::string8> track_info_list;

		command_result = "[";
		auto l = queue.get_count();
		for (auto k = 0; k < l; ++k)
		{
			queue[k].m_handle->format_title(NULL, track_info, track_script, NULL);
			pfc::splitStringSimple_toList(track_info_list, HTTPC_TF_SEP, track_info);
			command_result << "{";
			command_result << "\"pi\":\"" << queue[k].m_item << "\", ";
			command_result << "\"pl\":\"" << queue[k].m_playlist << "\", ";
			command_result << "\"qi\":\"" << k << "\", ";
			command_result << "\"t\":\"" << xml_friendly_string(track_info_list[0]) << "\", ";
			command_result << "\"a\":\"" << xml_friendly_string(track_info_list[1]) << "\", ";
			command_result << "\"al\":\"" << xml_friendly_string(track_info_list[2]) << "\", ";
			command_result << "\"l\":\"" << track_info_list[3] << "\", ";
			command_result << "\"r\":\"" << track_info_list[4] << "\" ";
			command_result << "}";
			if (k < l - 1)
			{
				command_result << ", ";
				track_info.reset();
				track_info_list.remove_all();
			}
		}
		command_result << "]";
		return true;
	}

	bool power_action(pfc::stringLite action)
	{
		HANDLE hToken;
		TOKEN_PRIVILEGES tkp;

		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
			return false;

		LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
		tkp.PrivilegeCount = 1;
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

		if (GetLastError() != ERROR_SUCCESS)
			return false;

		if (action == "SUSPEND" || action == "HIBERNATE")
		{
			if (!SetSuspendState(action == "HIBERNATE", TRUE, FALSE))
			{
				if (!SetSystemPowerState(action == "SUSPEND", TRUE))
					return false;
			}
		}
		else
		{
			UINT flags = EWX_FORCEIFHUNG;

			if (action == "POWEROFF")
				flags |= EWX_POWEROFF;
			else if (action == "SHUTDOWN")
				flags |= EWX_SHUTDOWN;
			else if (action == "REBOOT")
				flags |= EWX_REBOOT;

			if (!ExitWindowsEx(flags, 0))
				return false;
		}
		tkp.Privileges[0].Attributes = 0;
		AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
		return true;
	}

	bool cmd_shutdown(foo_httpserver_command* cmd)
	{
		SYSTEM_POWER_CAPABILITIES spc = { 0 };
		GetPwrCapabilities(&spc);
		if (spc.SystemS5)
			return power_action("POWEROFF");
		else
			return power_action("SHUTDOWN");
	}

	bool cmd_reboot(foo_httpserver_command* cmd)
	{
		return power_action("REBOOT");
	}

	bool cmd_suspend(foo_httpserver_command* cmd)
	{
		SYSTEM_POWER_CAPABILITIES spc = { 0 };
		GetPwrCapabilities(&spc);
		if (spc.SystemS1 || spc.SystemS2 || spc.SystemS3)
			return power_action("SUSPEND");
		else
			return false;
	}

	bool cmd_hibernate(foo_httpserver_command* cmd)
	{
		SYSTEM_POWER_CAPABILITIES spc = { 0 };
		GetPwrCapabilities(&spc);
		if (spc.SystemS4)
			return power_action("HIBERNATE");
		else
			return false;
	}

	void gen_cmd_table()
	{
		commands["P"] = &cmd_switchplaylistpage;
		commands["SwitchPlaylistPage"] = &cmd_switchplaylistpage;
		commands["Stop"] = &cmd_stop;
		commands["Start"] = &cmd_start;
		commands["PlayOrPause"] = &cmd_playorpause;
		commands["StartPrevious"] = &cmd_startprevious;
		commands["StartNext"] = &cmd_startnext;
		commands["SeekPercent"] = &cmd_seekpercent;
		commands["SeekDelta"] = &cmd_seekdelta;
		commands["SeekSecond"] = &cmd_seeksecond;
		commands["Browse"] = &cmd_browse;
		commands["Search"] = &cmd_searchmedialibrary;
		commands["SearchMediaLibrary"] = &cmd_searchmedialibrary;
		commands["SetFocus"] = &cmd_setfocus;
		commands["StartRandom"] = &cmd_startrandom;
		commands["Volume"] = &cmd_volume;
		commands["VolumeDelta"] = &cmd_volume_delta;
		commands["VolumeDB"] = &cmd_volume_db;
		commands["VolumeDBDelta"] = &cmd_volume_db_delta;
		commands["VolumeMuteToggle"] = &cmd_volume_mute_toggle;
		commands["Parse"] = &cmd_parse;
		commands["RefreshPlayingInfo"] = &cmd_refreshplayinginfo;
		commands["Del"] = &cmd_del;
		commands["Move"] = &cmd_move;
		commands["SwitchPlaylist"] = &cmd_switchplaylist;
		commands["PlayingCommand"] = &cmd_playingcommand;
		commands["SelectionCommand"] = &cmd_selectioncommand;
		commands["EnqueueTrack"] = &cmd_queueitems;
		commands["QueueItems"] = &cmd_queueitems;
		commands["DequeueItems"] = &cmd_dequeueitems;
		commands["SAQ"] = &cmd_saq;
		commands["SAC"] = &cmd_sac;
		commands["FlushQueue"] = &cmd_flushqueue;
		commands["QueueAlbum"] = &cmd_queuealbum;
		commands["QueueRandomItems"] = &cmd_queuerandomitems;
		commands["QueryRetrace"] = &cmd_queryretrace;
		commands["QueryAdvance"] = &cmd_queryadvance;
		commands["CmdLine"] = &cmd_cmdline;
		commands["RemovePlaylist"] = &cmd_removeplaylist;
		commands["CreatePlaylist"] = &cmd_createplaylist;
		commands["RenamePlaylist"] = &cmd_renameplaylist;
		commands["FocusOnPlaying"] = &cmd_focusonplaying;
		commands["EmptyPlaylist"] = &cmd_emptyplaylist;
		commands["Undo"] = &cmd_undo;
		commands["Redo"] = &cmd_redo;
		commands["PlaybackOrder"] = &cmd_playbackorder;
		commands["PlaylistItemsPerPage"] = &cmd_playlistitemsperpage;
		commands["SetSelection"] = &cmd_setselection;
		commands["SortAscending"] = &cmd_sort_ascending;
		commands["SortDescending"] = &cmd_sort_descending;
		commands["FormatTitles"] = &cmd_formattitles;
		commands["Version"] = &cmd_version;
		commands["FoobarVersion"] = &cmd_foobarversion;
		commands["GetQueue"] = &cmd_getqueue;
		commands["Shutdown"] = &cmd_shutdown;
		commands["Reboot"] = &cmd_reboot;
		commands["Suspend"] = &cmd_suspend;
		commands["Hibernate"] = &cmd_hibernate;
		commands["SeekSecondDelta"] = &cmd_seekdelta;	// deprecated
		commands["Sort"] = &cmd_sort_ascending;			// deprecated
		commands["Seek"] = &cmd_seekpercent;			// deprecated
	}

}
}