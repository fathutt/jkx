//
// Saved game.
//


#include "jkx_saved_game.h"
#include "jkx_rle.h"
#include <algorithm>
#include <memory>
#include "jkx_saved_game_helper.h"
#include "qcommon/qcommon.h"
#include "server/server.h"


namespace jkx
{


SavedGame::SavedGame() :
		error_message_(),
		file_handle_(),
		io_buffer_(),
		saved_io_buffer_(),
		io_buffer_offset_(),
		saved_io_buffer_offset_(),
		rle_buffer_(),
		is_readable_(),
		is_writable_(),
		is_failed_()
{
}

SavedGame::~SavedGame()
{
	close();
}

bool SavedGame::open(
	const std::string& base_file_name)
{
	close();


	const std::string file_path = generate_path(
		base_file_name);

	bool is_succeed = true;

	static_cast<void>(::FS_FOpenFileRead(
		file_path.c_str(),
		&file_handle_,
		qtrue));

	if (file_handle_ == 0)
	{
		is_succeed = false;

		error_message_ =
			S_COLOR_RED "Failed to open a saved game file: \"" +
			file_path + "\".";

		::Com_DPrintf(
			"%s\n",
			error_message_.c_str());
	}

	if (is_succeed)
	{
		is_readable_ = true;
	}


	if (is_succeed)
	{
		SavedGameHelper saved_game(
			this);

		int sg_version = -1;

		if (saved_game.try_read_chunk<int32_t>(
			INT_ID('_', 'V', 'E', 'R'),
			sg_version))
		{
			if (sg_version != iSAVEGAME_VERSION)
			{
				is_succeed = false;

				::Com_Printf(
					S_COLOR_RED "File \"%s\" has version # %d (expecting %d)\n",
					base_file_name.c_str(),
					sg_version,
					iSAVEGAME_VERSION);
			}
		}
		else
		{
			is_succeed = false;

			::Com_Printf(
				S_COLOR_RED "Failed to read a version.\n");
		}
	}

	if (is_succeed)
	{
		SavedGameHelper saved_game(
			this);

		int sg_game = -1;
		const int this_game = (::Com_IsOutcast() ? 1 : 0);

		if (!saved_game.try_read_chunk<int32_t>(
			INT_ID('_', 'G', 'A', 'M'),
			sg_game))
		{
			is_succeed = false;

			::Com_Printf(
				S_COLOR_RED "Failed to read which game wrote \"%s\".\n",
				base_file_name.c_str());
		}
		else if (sg_game != this_game)
		{
			is_succeed = false;

			::Com_Printf(
				S_COLOR_RED "File \"%s\" was written by %s and this is %s.\n",
				base_file_name.c_str(),
				sg_game == 1 ? "Jedi Outcast" : "Jedi Academy",
				this_game == 1 ? "Jedi Outcast" : "Jedi Academy");
		}
	}

	if (!is_succeed)
	{
		close();
	}

	return is_succeed;
}

bool SavedGame::create(
	const std::string& base_file_name)
{
	close();


	remove(
		base_file_name);

	const std::string file_path = generate_path(
		base_file_name);

	file_handle_ = ::FS_FOpenFileWrite(
		file_path.c_str());

	if (file_handle_ == 0)
	{
		const std::string error_message =
			S_COLOR_RED "Failed to create a saved game file: \"" +
			file_path + "\".";

		::Com_Printf(
			"%s\n",
			error_message.c_str());

		return false;
	}


	is_writable_ = true;

	const int sg_version = iSAVEGAME_VERSION;

	SavedGameHelper sgsh(this);

	sgsh.write_chunk<int32_t>(
		INT_ID('_', 'V', 'E', 'R'),
		sg_version);

	// Which game wrote this file.
	//
	// One binary can start as either, and the two write different chunks into
	// the same format - so a save has to say which it is, or the wrong reader
	// walks it and reports the failure somewhere far from the cause. The
	// version above says how OLD the file is; this says what it IS.
	sgsh.write_chunk<int32_t>(
		INT_ID('_', 'G', 'A', 'M'),
		::Com_IsOutcast() ? 1 : 0);

	if (is_failed())
	{
		close();
		return false;
	}

	return true;
}

void SavedGame::close()
{
	if (file_handle_ != 0)
	{
		::FS_FCloseFile(file_handle_);
		file_handle_ = 0;
	}

	clear_error();
	reset_buffer();

	saved_io_buffer_.clear();
	saved_io_buffer_offset_ = 0;

	rle_buffer_.clear();

	is_readable_ = false;
	is_writable_ = false;
}

bool SavedGame::read_chunk(
	const uint32_t chunk_id)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	io_buffer_offset_ = 0;

	const std::string chunk_id_string = get_chunk_id_string(
		chunk_id);

	::Com_DPrintf(
		"Attempting read of chunk %s\n",
		chunk_id_string.c_str());

	uint32_t loaded_chunk_id = 0;
	uint32_t loaded_data_size = 0;

	int loaded_chunk_size = ::FS_Read(
		&loaded_chunk_id,
		static_cast<int>(sizeof(loaded_chunk_id)),
		file_handle_);

	loaded_chunk_size += ::FS_Read(
		&loaded_data_size,
		static_cast<int>(sizeof(loaded_data_size)),
		file_handle_);

	const bool is_compressed = (static_cast<int32_t>(loaded_data_size) < 0);

	if (is_compressed)
	{
		loaded_data_size = -static_cast<int32_t>(loaded_data_size);
	}

	// Make sure we are loading the correct chunk...
	//
	if (loaded_chunk_id != chunk_id)
	{
		is_failed_ = true;

		const std::string loaded_chunk_id_string = get_chunk_id_string(
			loaded_chunk_id);

		error_message_ =
			"Loaded chunk ID (" +
				loaded_chunk_id_string +
				") does not match requested chunk ID (" +
				chunk_id_string +
				").";

		return false;
	}

	uint32_t loaded_checksum = 0;

	if ( ::Com_IsOutcast() )
	{
		// Get checksum...
		//
		loaded_chunk_size += ::FS_Read(
			&loaded_checksum,
			static_cast<int>(sizeof(loaded_checksum)),
			file_handle_);
	}

	// Load in data and magic number...
	//
	uint32_t compressed_size = 0;

	if (is_compressed)
	{
		loaded_chunk_size += ::FS_Read(
			&compressed_size,
			static_cast<int>(sizeof(compressed_size)),
			file_handle_);

		rle_buffer_.resize(
			compressed_size);

		loaded_chunk_size += ::FS_Read(
			rle_buffer_.data(),
			compressed_size,
			file_handle_);

		io_buffer_.resize(
			loaded_data_size);

		if (!decompress(
			rle_buffer_,
			io_buffer_))
		{
			is_failed_ = true;

			error_message_ =
				"Compressed chunk (" +
					chunk_id_string +
					") does not decode to the size it claims.";

			return false;
		}
	}
	else
	{
		io_buffer_.resize(
			loaded_data_size);

		loaded_chunk_size += ::FS_Read(
			io_buffer_.data(),
			loaded_data_size,
			file_handle_);
	}

	uint32_t loaded_magic_value = 0;

	if ( ::Com_IsOutcast() )
	{
		loaded_chunk_size += ::FS_Read(
			&loaded_magic_value,
			static_cast<int>(sizeof(loaded_magic_value)),
			file_handle_);

		if (loaded_magic_value != get_jk2_magic_value())
		{
			is_failed_ = true;

			error_message_ =
				"Bad saved game magic for chunk " + chunk_id_string + ".";

			return false;
		}
	}
	else
	{
		// Get checksum...
		//
		loaded_chunk_size += ::FS_Read(
			&loaded_checksum,
			static_cast<int>(sizeof(loaded_checksum)),
			file_handle_);
	}

	// Make sure the checksums match...
	//
	const uint32_t checksum = ::Com_BlockChecksum(
		io_buffer_.data(),
		static_cast<int>(io_buffer_.size()));

	if (loaded_checksum != checksum)
	{
		is_failed_ = true;

		error_message_ =
			"Failed checksum check for chunk " + chunk_id_string + ".";

		return false;
	}

	// Make sure we didn't encounter any read errors...
	std::size_t ref_chunk_size =
		sizeof(loaded_chunk_id) +
		sizeof(loaded_data_size) +
		sizeof(loaded_checksum) +
		(is_compressed ? sizeof(compressed_size) : 0) +
		(is_compressed ? compressed_size : io_buffer_.size());

	if ( ::Com_IsOutcast() )
	{
		ref_chunk_size += sizeof(loaded_magic_value);
	}

	if (loaded_chunk_size != static_cast<int>(ref_chunk_size))
	{
		is_failed_ = true;

		error_message_ =
			"Error during loading chunk " + chunk_id_string + ".";

		return false;
	}

	return true;
}

bool SavedGame::is_all_data_read() const
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		return false;
	}

	return io_buffer_.size() == io_buffer_offset_;
}

void SavedGame::ensure_all_data_read()
{
	if (!is_all_data_read())
	{
		error_message_ = "Not all expected data read.";

		throw_error();
	}
}

bool SavedGame::write_chunk(
	const uint32_t chunk_id)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}


	const std::string chunk_id_string = get_chunk_id_string(
		chunk_id);

	::Com_DPrintf(
		"Attempting write of chunk %s\n",
		chunk_id_string.c_str());

	if (::sv_testsave->integer != 0)
	{
		return true;
	}

	const int src_size = static_cast<int>(io_buffer_.size());

	const uint32_t checksum = Com_BlockChecksum(
		io_buffer_.data(),
		src_size);

	uint32_t saved_chunk_size = ::FS_Write(
		&chunk_id,
		static_cast<int>(sizeof(chunk_id)),
		file_handle_);

	int compressed_size = -1;

	if (::sv_compress_saved_games->integer != 0)
	{
		compress(
			io_buffer_,
			rle_buffer_);

		if (rle_buffer_.size() < io_buffer_.size())
		{
			compressed_size = static_cast<int>(rle_buffer_.size());
		}
	}

	const uint32_t magic_value = get_jk2_magic_value();

	if (compressed_size > 0)
	{
		const int size = -static_cast<int>(io_buffer_.size());

		saved_chunk_size += ::FS_Write(
			&size,
			static_cast<int>(sizeof(size)),
			file_handle_);

		if ( ::Com_IsOutcast() )
		{
			saved_chunk_size += ::FS_Write(
				&checksum,
				static_cast<int>(sizeof(checksum)),
				file_handle_);
		}

		saved_chunk_size += ::FS_Write(
			&compressed_size,
			static_cast<int>(sizeof(compressed_size)),
			file_handle_);

		saved_chunk_size += ::FS_Write(
			rle_buffer_.data(),
			compressed_size,
			file_handle_);

		if ( ::Com_IsOutcast() )
		{
			saved_chunk_size += ::FS_Write(
				&magic_value,
				static_cast<int>(sizeof(magic_value)),
				file_handle_);
		}
		else
		{
			saved_chunk_size += ::FS_Write(
				&checksum,
				static_cast<int>(sizeof(checksum)),
				file_handle_);
		}

		std::size_t ref_chunk_size =
			sizeof(chunk_id) +
			sizeof(size) +
			sizeof(checksum) +
			sizeof(compressed_size) +
			compressed_size;

		if ( ::Com_IsOutcast() )
		{
			ref_chunk_size += sizeof(magic_value);
		}

		if (saved_chunk_size != ref_chunk_size)
		{
			is_failed_ = true;

			error_message_ = "Failed to write " + chunk_id_string + " chunk.";

			::Com_Printf(
				"%s%s\n",
				S_COLOR_RED,
				error_message_.c_str());

			return false;
		}
	}
	else
	{
		const uint32_t size = static_cast<uint32_t>(io_buffer_.size());

		saved_chunk_size += ::FS_Write(
			&size,
			static_cast<int>(sizeof(size)),
			file_handle_);

		if ( ::Com_IsOutcast() )
		{
			saved_chunk_size += ::FS_Write(
				&checksum,
				static_cast<int>(sizeof(checksum)),
				file_handle_);
		}

		saved_chunk_size += ::FS_Write(
			io_buffer_.data(),
			size,
			file_handle_);

		if ( ::Com_IsOutcast() )
		{
			saved_chunk_size += ::FS_Write(
				&magic_value,
				static_cast<int>(sizeof(magic_value)),
				file_handle_);
		}
		else
		{
			saved_chunk_size += ::FS_Write(
				&checksum,
				static_cast<int>(sizeof(checksum)),
				file_handle_);
		}

		std::size_t ref_chunk_size =
			sizeof(chunk_id) +
			sizeof(size) +
			sizeof(checksum) +
			size;

		if ( ::Com_IsOutcast() )
		{
			ref_chunk_size += sizeof(magic_value);
		}

		if (saved_chunk_size != ref_chunk_size)
		{
			is_failed_ = true;

			error_message_ = "Failed to write " + chunk_id_string + " chunk.";

			::Com_Printf(
				"%s%s\n",
				S_COLOR_RED,
				error_message_.c_str());

			return false;
		}
	}

	return true;
}

bool SavedGame::read(
	void* dst_data,
	int dst_size)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (!dst_data)
	{
		is_failed_ = true;
		error_message_ = "Null pointer.";
		return false;
	}

	if (dst_size < 0)
	{
		is_failed_ = true;
		error_message_ = "Negative size.";
		return false;
	}

	if (!is_readable_)
	{
		is_failed_ = true;
		error_message_ = "Not readable.";
		return false;
	}

	if (dst_size == 0)
	{
		return true;
	}

	if ((io_buffer_offset_ + dst_size) > io_buffer_.size())
	{
		is_failed_ = true;
		error_message_ = "Not enough data.";
		return false;
	}

	std::uninitialized_copy_n(
		&io_buffer_[io_buffer_offset_],
		dst_size,
		static_cast<uint8_t*>(dst_data));

	io_buffer_offset_ += dst_size;

	return true;
}

bool SavedGame::write(
	const void* src_data,
	int src_size)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (!src_data)
	{
		is_failed_ = true;
		error_message_ = "Null pointer.";
		return false;
	}

	if (src_size < 0)
	{
		is_failed_ = true;
		error_message_ = "Negative size.";
		return false;
	}

	if (!is_writable_)
	{
		is_failed_ = true;
		error_message_ = "Not writable.";
		return false;
	}

	if (src_size == 0)
	{
		return true;
	}

	const std::size_t new_buffer_size = io_buffer_offset_ + src_size;

	io_buffer_.resize(
		new_buffer_size);

	std::uninitialized_copy_n(
		static_cast<const uint8_t*>(src_data),
		src_size,
		&io_buffer_[io_buffer_offset_]);

	io_buffer_offset_ = new_buffer_size;

	return true;
}

bool SavedGame::is_failed() const
{
	return is_failed_;
}

bool SavedGame::skip(
	int count)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (!is_readable_ && !is_writable_)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (count < 0)
	{
		is_failed_ = true;
		error_message_ = "Negative count.";
		return false;
	}

	if (count == 0)
	{
		return true;
	}

	const std::size_t new_offset = io_buffer_offset_ + count;
	const std::size_t buffer_size = io_buffer_.size();

	if (new_offset > buffer_size)
	{
		if (is_readable_)
		{
			is_failed_ = true;
			error_message_ = "Not enough data.";
			return false;
		}
		else if (is_writable_)
		{
			if (new_offset > buffer_size)
			{
				io_buffer_.resize(
					new_offset);
			}
		}
	}

	io_buffer_offset_ = new_offset;

	return true;
}

void SavedGame::save_buffer()
{
	saved_io_buffer_ = io_buffer_;
	saved_io_buffer_offset_ = io_buffer_offset_;
}

void SavedGame::load_buffer()
{
	io_buffer_ = saved_io_buffer_;
	io_buffer_offset_ = saved_io_buffer_offset_;
}

const void* SavedGame::get_buffer_data() const
{
	return io_buffer_.data();
}

int SavedGame::get_buffer_size() const
{
	return static_cast<int>(io_buffer_.size());
}

void SavedGame::rename(
	const std::string& old_base_file_name,
	const std::string& new_base_file_name)
{
	const std::string old_path = generate_path(
		old_base_file_name);

	const std::string new_path = generate_path(
		new_base_file_name);

	const int rename_result = ::FS_MoveUserGenFile(
		old_path.c_str(),
		new_path.c_str());

	if (rename_result == 0)
	{
		::Com_Printf(
			S_COLOR_RED "Error during savegame-rename."
				" Check \"%s\" for write-protect or disk full!\n",
			new_path.c_str());
	}
}

void SavedGame::remove(
	const std::string& base_file_name)
{
	const std::string path = generate_path(
		base_file_name);

	::FS_DeleteUserGenFile(
		path.c_str());
}

SavedGame& SavedGame::get_instance()
{
	static SavedGame result;
	return result;
}

void SavedGame::clear_error()
{
	is_failed_ = false;
	error_message_.clear();
}

void SavedGame::throw_error()
{
	if (error_message_.empty())
	{
		error_message_ = "Generic error.";
	}

	error_message_ = "SG: " + error_message_;

	::Com_Error(
		ERR_DROP,
		"%s",
		error_message_.c_str());
}

void SavedGame::compress(
	const Buffer& src_buffer,
	Buffer& dst_buffer)
{
	// The coding itself is in jkx_rle.cpp, which depends on nothing and can
	// therefore be handed malformed input by a test. See the comment at the top
	// of jkx_rle.h for what it used to do with any.
	dst_buffer.resize(
		RLE_MaxEncodedSize(src_buffer.size()));

	const size_t written = ::RLE_Encode(
		src_buffer.data(),
		src_buffer.size(),
		dst_buffer.data(),
		dst_buffer.size());

	dst_buffer.resize(written);
}

bool SavedGame::decompress(
	const Buffer& src_buffer,
	Buffer& dst_buffer)
{
	return ::RLE_Decode(
		src_buffer.data(),
		src_buffer.size(),
		dst_buffer.data(),
		dst_buffer.size());
}

std::string SavedGame::generate_path(
	const std::string& base_file_name)
{
	std::string normalized_file_name = base_file_name;

	std::replace(
		normalized_file_name.begin(),
		normalized_file_name.end(),
		'/',
		'_');

	return "saves/" + normalized_file_name + ".sav";
}

std::string SavedGame::get_chunk_id_string(
	uint32_t chunk_id)
{
	std::string result(4, '\0');

	result[0] = static_cast<char>((chunk_id >> 24) & 0xFF);
	result[1] = static_cast<char>((chunk_id >> 16) & 0xFF);
	result[2] = static_cast<char>((chunk_id >> 8) & 0xFF);
	result[3] = static_cast<char>((chunk_id >> 0) & 0xFF);

	return result;
}

void SavedGame::reset_buffer()
{
	io_buffer_.clear();
	reset_buffer_offset();
}

void SavedGame::reset_buffer_offset()
{
	io_buffer_offset_ = 0;
}

const uint32_t SavedGame::get_jk2_magic_value()
{
	return 0x1234ABCD;
}


} // jkx
