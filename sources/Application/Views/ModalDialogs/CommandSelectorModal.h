#ifndef _COMMAND_SELECTOR_MODAL_H_
#define _COMMAND_SELECTOR_MODAL_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include "Application/Views/BaseClasses/View.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Views/CommandSelectorCommon.h"
#include "Application/Views/CommandContext.h"

class CommandSelectorModal : public ModalView {
  public:
    CommandSelectorModal(View &parentView, FourCC *liveTarget,
                        ModalViewCallback previewCb = 0);
    virtual ~CommandSelectorModal();
    // Where the cell is and which slot its commands go to: commands that
    // won't do anything there are dimmed. Unknown slot in a phrase by default
    void SetContext(const CommandContext &context);

    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();

  private:
    void navigateGrid(int deltaRow, int deltaCol);
    void moveToCommand(FourCC command);
    int commandToRow(FourCC command) const;
    int commandToCol(FourCC command) const;
    FourCC cellAtGridPos(int row, int col) const;

    int selectedRow_;
    int selectedCol_;
    FourCC selectedCommand_;
    View &parentView_;
    FourCC *liveTarget_;
    FourCC savedCmd_;
    ModalViewCallback previewCb_;
    CommandContext context_;

    static const int GRID_COLUMNS = CommandSelectorCommon::kColumns;
};

#endif
