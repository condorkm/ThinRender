/*
 *  thin render - Mobile render engine based on OpenGL ES 2.0
 *  Copyright (C) 2013  Fundaci� i2CAT, Internet i Innovaci� digital a Catalunya
 *
 *  This file is part of thin render.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Author:         Marc Fernandez Vanaclocha <marc.fernandez@i2cat.net>
 */

#include <algorithm>
#include "Text.h"
#include "../fileSystem/FileSystem.h"
#include "../globalData/GlobalData.h"

void printShaderLog(GLuint object){
	GLint log_length = 0;
	if (glIsShader(object))
		glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else if (glIsProgram(object))
		glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else {
		logErr("printlog: Not a shader or a program\n");
		return;
	}

	char* log = (char*)malloc(log_length);

	if (glIsShader(object))
		glGetShaderInfoLog(object, log_length, NULL, log);
	else if (glIsProgram(object))
		glGetProgramInfoLog(object, log_length, NULL, log);

	logErr("printShaderLog %s", log);
	free(log);
}

Text::Text(Text* text) {
	maxHeight = text->maxHeight;
    c = new glyphData[128];
	vbo = 0;
	color = glm::vec4(0.0f,0.0f,0.0f,1.0f);
	programText = text->programText;
	uniform_tex = text->uniform_tex;
	attribute_coord = text->attribute_coord;
	uniform_color = text->uniform_color;
	uniform_pvmMatrix = text->uniform_pvmMatrix;
	tex = text->tex;
	w = text->w;
	h = text->h;
	memcpy(c, text->c, sizeof(glyphData)*128);
	atlasWidth = w;
	atlasHeight = h;
}
Text::Text(FT_Face face, int height) {
    c = new glyphData[128];
	color = glm::vec4(0.0f,0.0f,0.0f,1.0f);
	cIndex = 0;
	GLuint vs, fs;
	GLint compile_ok = GL_FALSE, link_ok = GL_FALSE;
	if(FileSystem::getInstance()->openFile("text.v.glsl") == -1){
		logErr("Error opening text.v.glsl");
		return;
	}
	if(FileSystem::getInstance()->openFile("text.f.glsl") == -1){
		logErr("Error opening text.f.glsl");
		return;
	}
	const char* source = FileSystem::getInstance()->getFileData("text.v.glsl");
	if (source == NULL) {
		logErr("Error data text.v.glsl");
		return;
	}

	vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &source, NULL);

	glCompileShader(vs);
	compile_ok = GL_FALSE;
	glGetShaderiv(vs, GL_COMPILE_STATUS, &compile_ok);
	if (compile_ok == GL_FALSE) {
		logErr("text.v.glsl");
		printShaderLog(vs);
		glDeleteShader(vs);
		return;
	}

	source = FileSystem::getInstance()->getFileData("text.f.glsl");
	if (source == NULL) {
		logErr("Error data text.f.glsl");
		return;
	}

	fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &source, NULL);

	glCompileShader(fs);
	compile_ok = GL_FALSE;
	glGetShaderiv(fs, GL_COMPILE_STATUS, &compile_ok);
	if (compile_ok == GL_FALSE) {
		logErr("text.v.glsl");
		printShaderLog(fs);
		glDeleteShader(fs);
		return;
	}

	programText = glCreateProgram();
	glAttachShader(programText, vs);
	glAttachShader(programText, fs);
	glLinkProgram(programText);

    glDeleteShader(vs);//these are marked to delete but until they are attached to the program, they will remain 
    glDeleteShader(fs);
    
	glGetProgramiv(programText, GL_LINK_STATUS, &link_ok);
	if (!link_ok) {
		logWar("glLinkProgram:");
		return;
	}

    std::string attributeName = "coord";
	attribute_coord = glGetAttribLocation(programText, attributeName.c_str());
	if (attribute_coord == -1) {
		logErr("Could not bind attribute %s\n", attributeName.c_str());
		return;
	}

	attributeName = "tex";
	uniform_tex = glGetUniformLocation(programText, attributeName.c_str());
	if (uniform_tex == -1) {
		logErr("Could not bind uniform %s\n", attributeName.c_str());
		return;
	}
	attributeName = "color";
	uniform_color = glGetUniformLocation(programText, attributeName.c_str());
	if (uniform_color == -1) {
		logErr("Could not bind uniform %s\n", attributeName.c_str());
		return;
	}

	attributeName = "pvmMatrix";
	uniform_pvmMatrix = glGetUniformLocation(programText, attributeName.c_str());
	if (uniform_pvmMatrix == -1) {
		logErr("Could not bind uniform %s\n", attributeName.c_str());
		return;
	}

	vbo = 0;

	FT_Set_Pixel_Sizes(face, 0, height);
	FT_GlyphSlot g = face->glyph;
	int roww = 0;
	int rowh = 0;
	w = 0;
	h = 0;
	memset(c, 0, sizeof(glyphData) * 128);

	/* Find minimum size for a texture holding all visible ASCII characters */
	for (int i = 32; i < 128; i++) {
		if (FT_Load_Char(face, i, FT_LOAD_RENDER)) {
			logErr("Loading character %c failed!\n", i);
			continue;
		}
		if (roww + g->bitmap.width + 1 >= MAXWIDTH) {
			w = std::max(w, roww);
			h += rowh;
			roww = 0;
			rowh = 0;
		}
		roww += g->bitmap.width + 1;
		rowh = std::max(rowh, g->bitmap.rows);
	}
	maxHeight = rowh;

	w = std::max(w, roww);
	h += rowh;
	/* Create a texture that will be used to hold all ASCII glyphs */
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, 0);

	/* We require 1 byte alignment when uploading texture data */
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	/* Clamping to edges is important to prevent artifacts when scaling */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/* Linear filtering usually looks best for text */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


	/* Paste all glyph bitmaps into the texture, remembering the offset */
	int ox = 0;
	int oy = 0;
	rowh = 0;

	for (int i = 32; i < 128; i++) {
		if (FT_Load_Char(face, i, FT_LOAD_RENDER)) {
			logErr("Loading character %c failed!\n", i);
			continue;
		}
		if (ox + g->bitmap.width + 1 >= MAXWIDTH) {
			oy += rowh+1;
			rowh = 0;
			ox = 0;
		}
		glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, g->bitmap.width, g->bitmap.rows, GL_ALPHA, GL_UNSIGNED_BYTE, g->bitmap.buffer);
		c[i].ax = g->advance.x >> 6;
		c[i].ay = g->advance.y >> 6;
		c[i].bw = g->bitmap.width;
		c[i].bh = g->bitmap.rows;
		c[i].bl = g->bitmap_left;
		c[i].bt = g->bitmap_top;
		c[i].tx = ox / (float)w;
		c[i].ty = oy / (float)h;
		rowh = std::max(rowh, g->bitmap.rows);
		ox += g->bitmap.width + 1;
	}
	logErr("Generated a %d x %d (%d kb) texture atlas\n", w, h, w * h / 1024);
	atlasWidth = w;
	atlasHeight = h;
}

Text::~Text() {
	if(vbo != 0) glDeleteBuffers(1, &vbo);
	glDeleteTextures(1, &tex);
    glUseProgram(0);
	glDeleteProgram(programText);
    delete[] c;
}


float Text::calculateLength(const char *start, const char *end){
	const uint8_t *p;
	float x = 0.0f;
	for (p = (const uint8_t *)start; *p; p++) {
		if(p == (const uint8_t *)end) return x;
		x += c[*p].ax;
	}
	return x;
}
int Text::calculateNumWhiteSpaces(const char *start, const char *end){
	const uint8_t *p;
	int res = 0;
	for (p = (const uint8_t *)start; p != (const uint8_t *)end; p++) {
		if(*p == ' ') res ++;
	}
	return res;
}
//fuck comments :D
uint8_t* Text::calculatePointerEndLine(const char *text, float width){
	uint8_t *p = (uint8_t *)text;
	uint8_t *candidate = p;
	float xPos = 0.0f;
	for (; *p; p++) {
		if(*p == '\n'){
			return p;
		}
		if(*p == '\\' && *(p+1) == 'n'){
			return p;
		}
		if(*p == ' '){
			candidate = p;
		}
		xPos += c[*p].ax;
		if(xPos>=width){
			return candidate;
		}
	}
	return p;
}
//first only left alignment
//align 0 left, 1 center, 2 justified
void Text::setBlockText(const char *text, float width, int align){
	logInf("setBlockText width: %f", width);
	float x = 0.0;
	float y = GlobalData::getInstance()->screenHeight - maxHeight;

	if(vbo != 0) glDeleteBuffers(1, &vbo);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	const uint8_t *p;

	uint8_t *lineEnding;
	//logWar("Text::setText message: %s x: %f y: %f sx: %f sy: %f", text, x, y, sx, sy);

	point coords[6 * strlen(text)];
	cIndex = 0;
	float line = 0.0f;
	lineEnding = calculatePointerEndLine(text, width);
	/* Loop through all characters */
	float whiteSpaceModificator = 1.0f;
	if(align == 2){//justificat
		float messageLength = calculateLength(text,(const char *)lineEnding);
		float numWhiteSpaces = calculateNumWhiteSpaces(text,(const char *)lineEnding);
		float advanceWhiteSpace = c[' '].ax;

		whiteSpaceModificator = (width - messageLength + numWhiteSpaces*advanceWhiteSpace)/(numWhiteSpaces*advanceWhiteSpace);
		if(whiteSpaceModificator >= 2.0f){
			whiteSpaceModificator = 1.0f;
		}
	}
	for (p = (const uint8_t *)text; *p; p++) {
		if(p == lineEnding){
			line--;
			if(*p == '\\') p++;
			p++;
			lineEnding = calculatePointerEndLine((const char*)p, width);
			x = 0.0f;
			if(align == 1){//center
				x = width/2.0f-calculateLength((const char *)p,(const char *)lineEnding)/2.0f;
			}else if(align == 2){//justificat
				float messageLength = calculateLength((const char *)p,(const char *)lineEnding);
				float numWhiteSpaces = calculateNumWhiteSpaces((const char *)p,(const char *)lineEnding);
				float advanceWhiteSpace = c[' '].ax;

				whiteSpaceModificator = (width - messageLength + numWhiteSpaces*advanceWhiteSpace)/(numWhiteSpaces*advanceWhiteSpace);
				if(whiteSpaceModificator >= 2.0f){
					whiteSpaceModificator = 1.0f;
				}
			}
		}
		/* Calculate the vertex and texture coordinates */
		float x2 = x + c[*p].bl;//bl bitmap left
		float y2 = -y - (c[*p].bt  + line * maxHeight);//bt bitmap top
		float w = c[*p].bw;//bw bitmap width
		float h = c[*p].bh;//bh bitmap height
		//logInf("caracter A bl bt bw bh ty tx = {%f, %f, %f, %f, %f, %f}", c[*p].bl, c[*p].bt, c[*p].bw, c[*p].bh, c[*p].ty, c[*p].tx);
		/* Advance the cursor to the start of the next character */

		if(*p == ' '){
			x += c[*p].ax*whiteSpaceModificator;//advance x
		}else{
			x += c[*p].ax;//advance x
		}
		y += c[*p].ay;//advance y

		/* Skip glyphs that have no pixels */
		if (!w || !h)
			continue;
		float marginXTexture = 0.5f/(float)atlasWidth;
		float marginYTexture = 0.5f/(float)atlasHeight;
		coords[cIndex++] = (point) {x2 + w, 	-y2,		c[*p].tx + c[*p].bw/(float)atlasWidth - marginXTexture,		c[*p].ty + marginYTexture};
		coords[cIndex++] = (point) {x2, 		-y2,		c[*p].tx + marginXTexture,									c[*p].ty + marginYTexture};
		coords[cIndex++] = (point) {x2, 		-y2 - h,	c[*p].tx + marginXTexture,									c[*p].ty + c[*p].bh / (float)atlasHeight - marginYTexture};
		coords[cIndex++] = (point) {x2 + w, 	-y2,		c[*p].tx + c[*p].bw/(float)atlasWidth - marginXTexture,		c[*p].ty + marginYTexture};
		coords[cIndex++] = (point) {x2, 		-y2 - h,	c[*p].tx + marginXTexture,									c[*p].ty + c[*p].bh / (float)atlasHeight - marginYTexture};
		coords[cIndex++] = (point) {x2 + w, 	-y2 - h,	c[*p].tx + c[*p].bw/(float)atlasWidth - marginXTexture,		c[*p].ty + c[*p].bh / (float)atlasHeight - marginYTexture};
	}
	glBufferData(GL_ARRAY_BUFFER, sizeof(coords), coords, GL_STATIC_DRAW);
}
void Text::setText(const char *text, float x, float y){
	if(vbo != 0) glDeleteBuffers(1, &vbo);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	const uint8_t *p;
	//logWar("Text::setText message: %s x: %f y: %f sx: %f sy: %f", text, x, y, sx, sy);

	point coords[6 * strlen(text)];
	cIndex = 0;
	/* Loop through all characters */
	for (p = (const uint8_t *)text; *p; p++) {
		/* Calculate the vertex and texture coordinates */
		float x2 = x + c[*p].bl;//bl bitmap left
		float y2 = -y - c[*p].bt;//bt bitmap top
		float w = c[*p].bw;//bw bitmap width
		float h = c[*p].bh;//bh bitmap height
		//logInf("caracter A bl bt bw bh ty tx = {%f, %f, %f, %f, %f, %f}", c[*p].bl, c[*p].bt, c[*p].bw, c[*p].bh, c[*p].ty, c[*p].tx);
		/* Advance the cursor to the start of the next character */
		x += c[*p].ax;//advance x
		y += c[*p].ay;//advance y

		/* Skip glyphs that have no pixels */
		if (!w || !h)
			continue;
		float marginXTexture = 0.5f/(float)atlasWidth;
		float marginYTexture = 0.5f/(float)atlasHeight;
		coords[cIndex++] = (point) {x2 + w, 	-y2,		c[*p].tx + c[*p].bw/(float)atlasWidth - marginXTexture,		c[*p].ty + marginYTexture};
		coords[cIndex++] = (point) {x2, 		-y2,		c[*p].tx + marginXTexture,									c[*p].ty + marginYTexture};
		coords[cIndex++] = (point) {x2, 		-y2 - h,	c[*p].tx + marginXTexture,									c[*p].ty + c[*p].bh / (float)atlasHeight - marginYTexture};
		coords[cIndex++] = (point) {x2 + w, 	-y2,		c[*p].tx + c[*p].bw/(float)atlasWidth - marginXTexture,		c[*p].ty + marginYTexture};
		coords[cIndex++] = (point) {x2, 		-y2 - h,	c[*p].tx + marginXTexture,									c[*p].ty + c[*p].bh / (float)atlasHeight - marginYTexture};
		coords[cIndex++] = (point) {x2 + w, 	-y2 - h,	c[*p].tx + c[*p].bw/(float)atlasWidth - marginXTexture,		c[*p].ty + c[*p].bh / (float)atlasHeight - marginYTexture};
	}
	glBufferData(GL_ARRAY_BUFFER, sizeof(coords), coords, GL_STATIC_DRAW);
}
void Text::render() {
	glUseProgram(programText);

	/* Use the texture containing the atlas */
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(uniform_tex, 0);
	glUniform4f(uniform_color, color.x, color.y, color.z, color.w);

	/* Set up the VBO for our vertex data */
	glEnableVertexAttribArray(attribute_coord);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

	/* Draw all the character on the screen in one go */

	glDrawArrays(GL_TRIANGLES, 0, cIndex);
	glDisableVertexAttribArray(attribute_coord);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Text::render(glm::mat4 pvmMatrix) {
	glUseProgram(programText);

	/* Use the texture containing the atlas */
	glUniformMatrix4fv(uniform_pvmMatrix, 1, GL_FALSE, glm::value_ptr(pvmMatrix));
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(uniform_tex, 0);
	glUniform4f(uniform_color, color.x, color.y, color.z, color.w);


	/* Set up the VBO for our vertex data */
	glEnableVertexAttribArray(attribute_coord);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

	/* Draw all the character on the screen in one go */

	glDrawArrays(GL_TRIANGLES, 0, cIndex);
	glDisableVertexAttribArray(attribute_coord);
	glBindTexture(GL_TEXTURE_2D, 0);
}

float Text::getWidthOfMessage(const char* message){
	const uint8_t *p;
	float x = 0.0f;
	for (p = (const uint8_t *)message; *p; p++) {
		x += c[*p].ax;
	}
	return x;
}
float Text::getHeightOfMessage(const char* message){
	const uint8_t *p;
	float y = 0.0f;
	float auxiliarY = 0.0f;
	for (p = (const uint8_t *)message; *p; p++) {
		auxiliarY = c[*p].bt;
		if(auxiliarY > y) y = auxiliarY;
	}
	return y;
}
