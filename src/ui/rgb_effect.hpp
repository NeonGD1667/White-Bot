#pragma once
#include "../includes.hpp"

// Node tiện ích: chạy 1 vòng lặp update, đổi màu dần theo bánh xe HSV (hue
// quay đều 360 độ) rồi áp màu đó lên target.
//
// Có 2 chế độ:
//
// 1) Chế độ danh sách (như cũ):
//      RGBEffect* fx = RGBEffect::create({ a, b, c }, 60.f, 0.8f, 0.55f);
//      this->addChild(fx);
//    -> chỉ đổi màu đúng các node trong danh sách.
//
// 2) Chế độ cả cây (mới):
//      m_mainLayer->addChild(RGBEffect::createTree(60.f, 0.8f, 0.75f, 10.f));
//    -> đổi màu TOÀN BỘ node con (đệ quy) của node cha mà effect được add vào.
//    Mỗi frame effect duyệt lại cây, nên node tạo muộn (ví dụ khi chuyển trang
//    settings) cũng tự được tô màu. Quy ước:
//      - node có ID bắt đầu bằng "no-rgb" bị bỏ qua cùng toàn bộ node con của nó
//      - CCLabelBMFont mặc định KHÔNG bị tô (giữ chữ dễ đọc), bật bằng tintLabels
//      - CCLayerColor bị bỏ qua
//    `spread` = số độ hue lệch theo mỗi 100 đơn vị toạ độ X (0 = cả UI cùng màu,
//    > 0 = dải màu chạy ngang UI).
//
// Muốn tắt hiệu ứng: fx->removeFromParentAndCleanup(true) rồi dựng lại UI,
// vì effect không lưu màu gốc để khôi phục.

class RGBEffect : public CCNode {
private:
    std::vector<CCNode*> targets;
    bool treeMode = false;
    bool tintLabels = false;
    float speed = 60.f; // độ/giây trên bánh xe hue (60 = full vòng trong 6s)
    float hue = 0.f;
    float saturation = 1.f;
    float value = 1.f;
    float spread = 0.f; // độ hue / 100 đơn vị X (chỉ chế độ cả cây)

    static ccColor3B hsvToRgb(float h, float s, float v) {
        h = std::fmod(h, 360.f);
        if (h < 0.f) h += 360.f;

        float c = v * s;
        float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
        float m = v - c;

        float r = 0.f, g = 0.f, b = 0.f;
        if (h < 60.f)       { r = c; g = x; b = 0.f; }
        else if (h < 120.f) { r = x; g = c; b = 0.f; }
        else if (h < 180.f) { r = 0.f; g = c; b = x; }
        else if (h < 240.f) { r = 0.f; g = x; b = c; }
        else if (h < 300.f) { r = x; g = 0.f; b = c; }
        else                { r = c; g = 0.f; b = x; }

        return ccc3(
            static_cast<GLubyte>((r + m) * 255.f),
            static_cast<GLubyte>((g + m) * 255.f),
            static_cast<GLubyte>((b + m) * 255.f)
        );
    }

    void tintTree(CCNode* node, bool isRoot) {
        if (!node || node == this) return;

        // Bỏ qua cả nhánh nếu được đánh dấu.
        if (std::string_view(node->getID()).rfind("no-rgb", 0) == 0) return;

        if (!isRoot) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                bool skip = typeinfo_cast<CCLayerColor*>(node) != nullptr ||
                            (!tintLabels && typeinfo_cast<CCLabelBMFont*>(node) != nullptr);

                if (!skip) {
                    float offset = 0.f;
                    if (spread != 0.f) {
                        if (auto* parent = node->getParent()) {
                            offset = spread * parent->convertToWorldSpace(node->getPosition()).x / 100.f;
                        }
                    }
                    rgba->setColor(hsvToRgb(hue + offset, saturation, value));
                }
            }
        }

        for (auto* child : node->getChildrenExt<CCNode*>())
            tintTree(child, false);
    }

public:
    // Chế độ danh sách.
    // targets: danh sách node sẽ bị đổi màu mỗi frame
    // speed: tốc độ quay hue, độ/giây (mặc định 60 -> full chu kỳ màu trong 6s)
    // saturation/value: độ bão hòa/độ sáng cố định, mặc định full màu tươi
    static RGBEffect* create(std::vector<CCNode*> const& targets,
                             float speed = 60.f, float saturation = 1.f, float value = 1.f) {
        RGBEffect* ret = new RGBEffect();
        if (ret->init()) {
            ret->targets = targets;
            ret->speed = speed;
            ret->saturation = saturation;
            ret->value = value;
            ret->scheduleUpdate();
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    // Chế độ cả cây: tô màu mọi node con của node cha mà effect được add vào.
    static RGBEffect* createTree(float speed = 60.f, float saturation = 1.f, float value = 1.f,
                                 float spread = 0.f, bool tintLabels = false) {
        RGBEffect* ret = new RGBEffect();
        if (ret->init()) {
            ret->treeMode = true;
            ret->speed = speed;
            ret->saturation = saturation;
            ret->value = value;
            ret->spread = spread;
            ret->tintLabels = tintLabels;
            ret->scheduleUpdate();
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    // Thêm target sau khi đã tạo (chỉ có tác dụng ở chế độ danh sách).
    void addTarget(CCNode* target) {
        if (target) targets.push_back(target);
    }

    void update(float dt) override {
        hue += speed * dt;
        if (hue >= 360.f) hue -= 360.f;

        if (treeMode) {
            tintTree(this->getParent(), true);
            return;
        }

        ccColor3B color = hsvToRgb(hue, saturation, value);

        for (CCNode* node : targets) {
            if (!node) continue;

            if (auto* sprite = typeinfo_cast<CCSprite*>(node))
                sprite->setColor(color);
            else if (auto* label = typeinfo_cast<CCLabelBMFont*>(node))
                label->setColor(color);
            else if (auto* scale9 = typeinfo_cast<CCScale9Sprite*>(node))
                scale9->setColor(color);
            // Các loại CCNode khác (không implement CCRGBAProtocol) bị bỏ
            // qua thay vì gọi setColor không tồn tại trên CCNode gốc.
        }
    }
};