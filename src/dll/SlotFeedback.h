#pragma once
#include "preyvr/SlotFeedback.h"
#include <vector>
namespace preyvr::dll {
struct GameplayPoseFrame;
struct TrackingFrame;
void PublishSlotFeedback(equipment::SlotNotice,const GameplayPoseFrame&);
bool ReadSlotFeedback(const TrackingFrame&,equipment::SlotMessage&);
void ClearSlotFeedback();
constexpr unsigned SlotFeedbackWidth=1024,SlotFeedbackHeight=224;
std::vector<std::uint8_t> DrawSlotFeedback(equipment::SlotNotice);
}
