#pragma once

#include "DialogHelper.h"
#include "VirtualListView.h"
#include "resource.h"
#include "ColumnManager.h"

class CSelectColumnsDlg :
	public CDialogImpl<CSelectColumnsDlg>,
	public CDialogHelper<CSelectColumnsDlg>,
	public CDynamicDialogLayout<CSelectColumnsDlg>,
	public CVirtualListView <CSelectColumnsDlg> {
public:
	enum { IDD = IDD_COLUMNS };

	CSelectColumnsDlg(ColumnManager* cm);

	CString GetColumnText(HWND, int row, int col) const;
	ListViewRowCheck IsRowChecked(HWND, int row) const;
	void DoSort(const SortInfo* si);

	BEGIN_MSG_MAP(CSelectColumnsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
		NOTIFY_CODE_HANDLER(NM_CLICK, OnListClick)
		CHAIN_MSG_MAP(CVirtualListView<CSelectColumnsDlg>)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CSelectColumnsDlg>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnListKeyDown(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnListClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);

	const ColumnManager::ColumnInfo& GetColumn(int index) const;
	void ToggleSelected();
	void CheckItemCategory(const CString& category, bool check);
	void CheckTreeChildren(HTREEITEM hParent, bool check);

private:
	struct Item {
		CString Name, Category;
		int Index;
		bool Visible;
	};
	std::vector<Item> m_Items;

	ColumnManager* m_ColMgr;
	CListViewCtrl m_List;
	CTreeViewCtrl m_Tree;
	inline static bool s_UseList{ true };
	bool m_Init{ true };
};

