#include <string>
#include <stdexcept>
#include <algorithm>
#include <SOIL.h>
#include <GL/gl.h>

#include "texture.hh"
#include "filesystem.hh"

GLuint load_texture(const std::string& filename, bool repeat) {
	GLuint handle = SOIL_load_OGL_texture
		(
			filename.c_str(),
			SOIL_LOAD_AUTO,
			SOIL_CREATE_NEW_ID,
			SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | (repeat ? SOIL_FLAG_TEXTURE_REPEATS : 0)
		);

	/* check for an error during the load process */
	if (0 == handle) throw std::runtime_error(std::string("SOIL couldn't load image ") + filename + std::string(": ") + SOIL_last_result());
	return handle;
}


TextureMap load_textures() {
	TextureMap tmap;
	tmap.insert(std::pair<std::string, GLuint>("title", load_texture(getFilePath("images/title.png"), true)));
	tmap.insert(std::pair<std::string, GLuint>("background", load_texture(getFilePath("images/bg.png"), true)));
	tmap.insert(std::pair<std::string, GLuint>("water", load_texture(getFilePath("images/water.png"), true)));
	tmap.insert(std::pair<std::string, GLuint>("ground", load_texture(getFilePath("images/ground.png"))));
	tmap.insert(std::pair<std::string, GLuint>("ladder", load_texture(getFilePath("images/ladder.png"))));
	tmap.insert(std::pair<std::string, GLuint>("crate", load_texture(getFilePath("images/crate.png"))));
	tmap.insert(std::pair<std::string, GLuint>("powerups", load_texture(getFilePath("images/powerups.png"))));
	tmap.insert(std::pair<std::string, GLuint>("tomato_1", load_texture(getFilePath("images/player_1.png"))));
	tmap.insert(std::pair<std::string, GLuint>("tomato_2", load_texture(getFilePath("images/player_2.png"))));
	tmap.insert(std::pair<std::string, GLuint>("tomato_3", load_texture(getFilePath("images/player_3.png"))));
	tmap.insert(std::pair<std::string, GLuint>("tomato_4", load_texture(getFilePath("images/player_4.png"))));

	return tmap;
}


float* getTileTexCoords(int tileid, int xtiles, int ytiles, bool horiz_flip, float xoff, float yoff) {
	static CoordArray tc;
	float tilew = 1.0f / xtiles;
	float tileh = 1.0f / ytiles;
	float x = (tileid % xtiles) * tilew + xoff;
	float y = 1.0 - int(tileid / xtiles) * tileh - yoff;
	tc.clear();
	if (horiz_flip) { // Flipped
		float temp[] = { x + tilew, y - tileh,
		                 x + tilew, y,
		                 x, y,
		                 x, y - tileh };
		tc.insert(tc.end(), &temp[0], &temp[8]);
	} else { // Non-flipped
		float temp[] = { x, y - tileh,
		                 x, y,
		                 x + tilew, y,
		                 x + tilew, y - tileh };
		tc.insert(tc.end(), &temp[0], &temp[8]);
	}
	return &tc[0];
}


void drawVertexArray(const float* v_a, const float* t_a, GLuint n, GLuint tex) {
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, t_a);
	glVertexPointer(2, GL_FLOAT, 0, v_a);
	glDrawArrays(GL_QUADS, 0, n);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisable(GL_TEXTURE_2D);
}

void drawImage(GLuint tex, int x, int y, int w, int h) {
	float vert[] = { (float)x, (float)(y + h),
		             (float)x, (float)y,
		             (float)(x + w), (float)y,
		             (float)(x + w), (float)(y + h) };
	drawVertexArray(&vert[0], &tex_square[0], 4, tex);
}
