#pragma once
#include "model.h"

struct UiState {
  const Snapshot* snap;     // nullptr = ยังไม่เคยได้รับข้อมูล
  uint32_t epochNow;        // เวลาปัจจุบัน (คำนวณจาก now ที่ PC ส่ง + millis)
  uint32_t secsSinceFrame;  // วินาทีนับจาก snapshot ล่าสุด
  bool linkStale;           // ไม่ได้รับข้อมูลนานเกินกำหนด
};

void uiBegin();
void uiRender(const UiState& st);  // วาดหน้าปัจจุบัน (เรียกทุกวินาทีได้ ไม่กระพริบ)
void uiNextPage();
int uiPageCount();

// ต้องเรียกหลัง tft.setRotation(): ปรับขนาดจอ/เลย์เอาต์ แล้ววาดใหม่ทั้งจอในรอบถัดไป
void uiOrientationChanged();

// พิกัดที่แตะ (หลังแปลงตามทิศจอแล้ว) โดนปุ่มหมุนจอข้างนาฬิกาหรือไม่ (เผื่อขอบให้กดง่าย)
bool uiHitRotateButton(int x, int y);

// ปุ่มสลับธีมสว่าง/มืด ซ้ายของปุ่มหมุนจอ
bool uiHitThemeButton(int x, int y);
void uiSetLightTheme(bool light);  // วาดใหม่ทั้งจอในรอบถัดไป
bool uiLightTheme();
