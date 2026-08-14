//
// Saved game specialized archivers
//


#ifndef JKX_SAVED_GAME_CLASS_ARCHIVERS_INCLUDED
#define JKX_SAVED_GAME_CLASS_ARCHIVERS_INCLUDED


#include "qcommon/q_math.h"
#include "jkx_saved_game_helper_fwd.h"


namespace jkx
{


template<>
class SavedGameClassArchiver<cplane_t>
{
public:
	enum { is_implemented = true };

	static void sg_export(
		SavedGameHelper& saved_game,
		const cplane_t& instance)
	{
		saved_game.write<float>(instance.normal);
		saved_game.write<float>(instance.dist);
		saved_game.write<uint8_t>(instance.type);
		saved_game.write<uint8_t>(instance.signbits);
		saved_game.write<uint8_t>(instance.pad);
	}

	static void sg_import(
		SavedGameHelper& saved_game,
		cplane_t& instance)
	{
		saved_game.read<float>(instance.normal);
		saved_game.read<float>(instance.dist);
		saved_game.read<uint8_t>(instance.type);
		saved_game.read<uint8_t>(instance.signbits);
		saved_game.read<uint8_t>(instance.pad);
	}
};


} // jkx


#endif // JKX_SAVED_GAME_CLASS_ARCHIVERS_INCLUDED
