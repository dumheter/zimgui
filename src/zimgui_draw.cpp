// This file extents imgui_draw.cpp with a few new functions.

#include "../deps/imgui/imgui_draw.cpp"

#define ZIMGUI_API extern "C"

ZIMGUI_API void zimgui_Ext_renderText(ImFont* font, ImDrawList* draw_list, float size, ImVec2 pos, const ImVec4& clip_rect, const char* text_begin, const char* text_end, float wrap_width, bool cpu_fine_clip, const ImU32* colorlen, ImU32 colorlen_len);

ZIMGUI_API void zimgui_Ext_addText(ImGuiContext* context, float font_size, float pos_x, float pos_y, const char* text, size_t textlen, float wrap_width, const float* fine_clip_rect_x, const float* fine_clip_rect_y, const float* fine_clip_rect_z, const float* fine_clip_rect_w, const unsigned int* colorlen, size_t colorlen_len)
{
  if (!textlen)
    return;

  ImFont* font = context->Font;
  ImDrawList* draw_list = context->CurrentWindow->DrawList;

  // Pull default font/size from the shared ImDrawListSharedData instance
  if (font == NULL)
    font = draw_list->_Data->Font;
  if (font_size == 0.0f)
    font_size = draw_list->_Data->FontSize;

  IM_ASSERT(font->ContainerAtlas->TexID == draw_list->_CmdHeader.TextureId);  // Use high-level ImGui::PushFont() or low-level ImDrawList::PushTextureId() to change font.

  ImVec4 clip_rect = draw_list->_CmdHeader.ClipRect;
  const bool cpu_fine_clip_rect = fine_clip_rect_x && fine_clip_rect_y && fine_clip_rect_z && fine_clip_rect_w;
  if (cpu_fine_clip_rect)
    {
      clip_rect.x = ImMax(clip_rect.x, *fine_clip_rect_x);
      clip_rect.y = ImMax(clip_rect.y, *fine_clip_rect_y);
      clip_rect.z = ImMin(clip_rect.z, *fine_clip_rect_z);
      clip_rect.w = ImMin(clip_rect.w, *fine_clip_rect_w);
    }
  zimgui_Ext_renderText(font, draw_list, font_size, {pos_x, pos_y}, clip_rect, text, text + textlen, wrap_width, cpu_fine_clip_rect, colorlen, colorlen_len);
}

// Note: as with every ImDrawList drawing function, this expects that the font atlas texture is bound.
ZIMGUI_API void zimgui_Ext_renderText(ImFont* font, ImDrawList* draw_list, float size, ImVec2 pos, const ImVec4& clip_rect, const char* text_begin, const char* text_end, float wrap_width, bool cpu_fine_clip, const ImU32* colorlen, ImU32 colorlen_len)
{
  if (!text_end)
    text_end = text_begin + strlen(text_begin); // ImGui:: functions generally already provides a valid text_end, so this is merely to handle direct calls.

  // Align to be pixel perfect
  pos.x = IM_FLOOR(pos.x);
  pos.y = IM_FLOOR(pos.y);
  float x = pos.x;
  float y = pos.y;
  if (y > clip_rect.w)
    return;

  const float scale = size / font->FontSize;
  const float line_height = font->FontSize * scale;
  const bool word_wrap_enabled = (wrap_width > 0.0f);
  const char* word_wrap_eol = NULL;

  // Fast-forward to first visible line
  const char* s = text_begin;
  if (y + line_height < clip_rect.y && !word_wrap_enabled)
    while (y + line_height < clip_rect.y && s < text_end)
      {
        s = (const char*)memchr(s, '\n', text_end - s);
        s = s ? s + 1 : text_end;
        y += line_height;
      }

  // For large text, scan for the last visible line in order to avoid over-reserving in the call to PrimReserve()
  // Note that very large horizontal line will still be affected by the issue (e.g. a one megabyte string buffer without a newline will likely crash atm)
  if (text_end - s > 10000 && !word_wrap_enabled)
    {
      const char* s_end = s;
      float y_end = y;
      while (y_end < clip_rect.w && s_end < text_end)
        {
          s_end = (const char*)memchr(s_end, '\n', text_end - s_end);
          s_end = s_end ? s_end + 1 : text_end;
          y_end += line_height;
        }
      text_end = s_end;
    }
  if (s == text_end)
    return;

  // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
  const int vtx_count_max = (int)(text_end - s) * 4;
  const int idx_count_max = (int)(text_end - s) * 6;
  const int idx_expected_size = draw_list->IdxBuffer.Size + idx_count_max;
  draw_list->PrimReserve(idx_count_max, vtx_count_max);

  ImDrawVert* vtx_write = draw_list->_VtxWritePtr;
  ImDrawIdx* idx_write = draw_list->_IdxWritePtr;
  unsigned int vtx_current_idx = draw_list->_VtxCurrentIdx;

  // Decode RGB from colorlen.
  const ImU32* colorlen_decoder = colorlen;
  ImU32 colorlen_decoder_sum =
    (colorlen_decoder == nullptr || (*colorlen_decoder & 0xFF00'0000) == 0)
    ? UINT_MAX
    : ((*colorlen_decoder & 0xFF00'0000) >> 24);

  while (s < text_end)
    {
      if (word_wrap_enabled)
        {
          // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
          if (!word_wrap_eol)
            {
              word_wrap_eol = font->CalcWordWrapPositionA(scale, s, text_end, wrap_width - (x - pos.x));
              if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
                word_wrap_eol++;    // +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
            }

          if (s >= word_wrap_eol)
            {
              x = pos.x;
              y += line_height;
              word_wrap_eol = NULL;

              // Wrapping skips upcoming blanks
              while (s < text_end)
                {
                  const char c = *s;
                  if (ImCharIsBlankA(c)) { s++; } else if (c == '\n') { s++; break; } else { break; }
                }
              continue;
            }
        }

      // Decode and advance source
      unsigned int c = (unsigned int)*s;
      if (c < 0x80)
        {
          s += 1;
        }
      else
        {
          s += ImTextCharFromUtf8(&c, s, text_end);
          if (c == 0) // Malformed UTF-8?
            break;
        }

      if (c < 32)
        {
          if (c == '\n')
            {
              x = pos.x;
              y += line_height;
              if (y > clip_rect.w)
                break; // break out of main loop
              continue;
            }
          if (c == '\r')
            continue;
        }

      const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
      if (glyph == NULL)
        continue;

      float char_width = glyph->AdvanceX * scale;
      if (glyph->Visible)
        {
          // We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
          float x1 = x + glyph->X0 * scale;
          float x2 = x + glyph->X1 * scale;
          float y1 = y + glyph->Y0 * scale;
          float y2 = y + glyph->Y1 * scale;
          if (x1 <= clip_rect.z && x2 >= clip_rect.x)
            {
              // Render a character
              float u1 = glyph->U0;
              float v1 = glyph->V0;
              float u2 = glyph->U1;
              float v2 = glyph->V1;

              // CPU side clipping used to fit text in their frame when the frame is too small. Only does clipping for axis aligned quads.
              if (cpu_fine_clip)
                {
                  if (x1 < clip_rect.x)
                    {
                      u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                      x1 = clip_rect.x;
                    }
                  if (y1 < clip_rect.y)
                    {
                      v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                      y1 = clip_rect.y;
                    }
                  if (x2 > clip_rect.z)
                    {
                      u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                      x2 = clip_rect.z;
                    }
                  if (y2 > clip_rect.w)
                    {
                      v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                      y2 = clip_rect.w;
                    }
                  if (y1 >= y2)
                    {
                      x += char_width;
                      continue;
                    }
                }

              // decode color
              const ImU32 text_index = static_cast<ImU32>(s - text_begin);
              while (text_index > colorlen_decoder_sum)
                {
                  colorlen_decoder++;
                  if (colorlen_decoder < colorlen_decoder + colorlen_len)
                    {
                      if ((*colorlen_decoder & 0xFF00'0000) == 0)
                        colorlen_decoder_sum = UINT_MAX;
                      else
                        colorlen_decoder_sum += (*colorlen_decoder & 0xFF00'0000) >> 24;
                    }
                }
              const ImU32 glyph_col = (*colorlen_decoder & 0x00FF'FFFF) + 0xFF00'0000;

              // We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a debug builds. Inlined here:
              {
                idx_write[0] = (ImDrawIdx)(vtx_current_idx); idx_write[1] = (ImDrawIdx)(vtx_current_idx+1); idx_write[2] = (ImDrawIdx)(vtx_current_idx+2);
                idx_write[3] = (ImDrawIdx)(vtx_current_idx); idx_write[4] = (ImDrawIdx)(vtx_current_idx+2); idx_write[5] = (ImDrawIdx)(vtx_current_idx+3);
                vtx_write[0].pos.x = x1; vtx_write[0].pos.y = y1; vtx_write[0].col = glyph_col; vtx_write[0].uv.x = u1; vtx_write[0].uv.y = v1;
                vtx_write[1].pos.x = x2; vtx_write[1].pos.y = y1; vtx_write[1].col = glyph_col; vtx_write[1].uv.x = u2; vtx_write[1].uv.y = v1;
                vtx_write[2].pos.x = x2; vtx_write[2].pos.y = y2; vtx_write[2].col = glyph_col; vtx_write[2].uv.x = u2; vtx_write[2].uv.y = v2;
                vtx_write[3].pos.x = x1; vtx_write[3].pos.y = y2; vtx_write[3].col = glyph_col; vtx_write[3].uv.x = u1; vtx_write[3].uv.y = v2;
                vtx_write += 4;
                vtx_current_idx += 4;
                idx_write += 6;
              }
            }
        }

      x += char_width;
    }

  // Give back unused vertices (clipped ones, blanks) ~ this is essentially a PrimUnreserve() action.
  draw_list->VtxBuffer.Size = (int)(vtx_write - draw_list->VtxBuffer.Data); // Same as calling shrink()
  draw_list->IdxBuffer.Size = (int)(idx_write - draw_list->IdxBuffer.Data);
  draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 1].ElemCount -= (idx_expected_size - draw_list->IdxBuffer.Size);
  draw_list->_VtxWritePtr = vtx_write;
  draw_list->_IdxWritePtr = idx_write;
  draw_list->_VtxCurrentIdx = vtx_current_idx;
}

ZIMGUI_API void zimgui_Ext_calcBbForCharInText(ImGuiContext* context, float font_size, float pos_x, float pos_y, const char* text_begin, size_t text_len, float wrap_width, size_t char_index, float* min_x, float* min_y, float* max_x, float* max_y)
{
  ImFont* font = context->Font;

  const char* text_end = text_begin + text_len;

  // Align to be pixel perfect
  pos_x = IM_FLOOR(pos_x);
  pos_y = IM_FLOOR(pos_y);
  float x = pos_x;
  float y = pos_y;
  // if (y > clip_rect.w)
  //     return;

  const float scale = font_size / font->FontSize;
  const float line_height = font->FontSize * scale;
  const bool word_wrap_enabled = (wrap_width > 0.0f);
  const char* word_wrap_eol = NULL;

  // Fast-forward to first visible line
  const char* s = text_begin;
  if (/*y + line_height < clip_rect.y &&*/ !word_wrap_enabled)
    while (/*y + line_height < clip_rect.y &&*/ s < text_end)
      {
        s = (const char*)memchr(s, '\n', text_end - s);
        s = s ? s + 1 : text_end;
        y += line_height;
      }

  // For large text, scan for the last visible line in order to avoid over-reserving in the call to PrimReserve()
  // Note that very large horizontal line will still be affected by the issue (e.g. a one megabyte string buffer without a newline will likely crash atm)
  if (text_end - s > 10000 && !word_wrap_enabled)
    {
      const char* s_end = s;
      //float y_end = y;
      while (/*y_end < clip_rect.w &&*/ s_end < text_end)
        {
          s_end = (const char*)memchr(s_end, '\n', text_end - s_end);
          s_end = s_end ? s_end + 1 : text_end;
          //y_end += line_height;
        }
      text_end = s_end;
    }
  if (s == text_end)
    return;

  bool search_index_found = false;

  while (s < text_end)
    {
      if (word_wrap_enabled)
        {
          // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
          if (!word_wrap_eol)
            {
              word_wrap_eol = font->CalcWordWrapPositionA(scale, s, text_end, wrap_width - (x - pos_x));
              if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
                word_wrap_eol++;    // +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
            }

          if (s >= word_wrap_eol)
            {
              x = pos_x;
              y += line_height;
              word_wrap_eol = NULL;

              // Wrapping skips upcoming blanks
              while (s < text_end)
                {
                  const char c = *s;
                  if (ImCharIsBlankA(c)) { s++; } else if (c == '\n') { s++; break; } else { break; }
                }
              continue;
            }
        }

      // Decode and advance source
      unsigned int c = (unsigned int)*s;
      if (c < 0x80)
        {
          s += 1;
        }
      else
        {
          s += ImTextCharFromUtf8(&c, s, text_end);
          if (c == 0) // Malformed UTF-8?
            break;
        }

      if (c < 32)
        {
          if (c == '\n')
            {
              x = pos_x;
              y += line_height;
              // if (y > clip_rect.w)
              //     break; // break out of main loop
              if (!search_index_found && ((text_begin + char_index) <= s))
                {
                  search_index_found = true;
                  if (char_index != 0) {
                    *min_x = x;
                    *min_y = y;
                    *max_x = x + font->FallbackAdvanceX;
                    *max_y = y + font_size;
                  }
                }
              continue;
            }
          if (c == '\r')
            continue;
        }

      const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
      if (glyph == NULL)
        continue;

      float char_width = glyph->AdvanceX * scale;
      x += char_width;

      if (!search_index_found && ((text_begin + char_index) <= s))
        {
          search_index_found = true;
          if (char_index != 0) {
            *min_x = x;
            *min_y = y;
            *max_x = x + font->FallbackAdvanceX;
            *max_y = y + font_size;
            // TODO cgustafsson: break?
          }
        }
    }
}

//
