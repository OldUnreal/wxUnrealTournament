#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/uiaction.h>
#include "WxNaturalSort.h"
#include "Core.h"
#include "Engine.h"
#include "FConfigCacheIni.h"

#define __HERE__  GLog->Logf(TEXT("Here: %d"), __LINE__);

class TreeItem {
public:
    TreeItem* Parent;
    TArray<TreeItem> Children;
    BOOL Loaded;

    FPreferencesInfo Prefs{};

    UProperty*      Property;
	INT				Offset;
	INT				ArrayIndex;

	UClass*  Class{};
	UBOOL	 Failed{};

    TreeItem(TreeItem* InParent, UProperty* InProperty, INT InOffset, INT InArrayIndex):
        Parent(InParent), Property(InProperty), Offset(InOffset), ArrayIndex(InArrayIndex), Loaded(false)
    {
    }

    TreeItem(FString InCaption):
        TreeItem(NULL, NULL, -1, -1)
    {
        Prefs.Caption = InCaption;
    }

    TreeItem(TreeItem* InParent, FPreferencesInfo InPrefs):
        TreeItem(InParent, NULL, -1, -1)
    {
        Prefs = InPrefs;
    }

    TreeItem(TreeItem* InParent, UProperty* InProperty, FString InCaption, INT InOffset, INT InArrayIndex):
        TreeItem(InParent, InProperty, InOffset, InArrayIndex)
    {
        Prefs.Caption = InCaption;
    }

    FString GetCaption()
    {
        guard(TreeItem::GetCaption);

        return ArrayIndex == -1 ? Prefs.Caption :
            FString::Printf( TEXT("[%i]"), ArrayIndex );

        unguard;
    }

    void GetPropertyTextSafe(FString& Str, BYTE* ReadValue)
	{
		guard(TreeItem::GetPropertyTextSafe);
		if (Cast<UClassProperty>(Property) && appStricmp(*Property->Category, TEXT("Drivers")) == 0)
		{
			// Class config.
			FString Path, Left, Right;
			GConfig->GetString(*FObjectPathName(Property->GetOwnerClass()), Property->GetName(), Path);
			if (Path.Split(TEXT("."), &Left, &Right))
				Str = Localize(*Right, TEXT("ClassCaption"), *Left);
			else
				Str = Localize("Language", "Language", TEXT("Core"), *Path);
		}
		else
		{
			// Regular property.
			Property->ExportText(0, Str, ReadValue - Property->Offset, ReadValue - Property->Offset, PPF_Localized);
		}
		unguard;
	}

    FString GetValue()
    {
        guard(TreeItem::GetValue);

        BYTE* ReadValue = GetReadAddress( Property );

        if (Property)
        {
            if( (Property->ArrayDim!=1 && ArrayIndex==-1) || Cast<UArrayProperty>(Property) )
            {
                // Array expander.
                return TEXT("...");
            }
            /* TODO
            else if( ReadValue && Cast<UStructProperty>(Property) && Cast<UStructProperty>(Property)->Struct->GetFName()==NAME_Color )
            {
                // Color.
                FillRect( hDC, RightRect + FRect(4,4,-4,-4).DPIScaled(WWindow::DPIX, WWindow::DPIY), hBrushBlack );
                HBRUSH hBrush = CreateSolidBrush(COLORREF(*(DWORD*)ReadValue));
                FillRect( hDC, RightRect + FRect(5,5,-5,-5).DPIScaled(WWindow::DPIX, WWindow::DPIY), hBrush );
                DeleteObject( hBrush );
            }
            */
            else if( ReadValue )
            {
                // Text.
                FString Str = TEXT("");
                GetPropertyTextSafe( Str, ReadValue );
                return Str;
            }
        }

        return TEXT("");

        unguard;
    }

    UProperty* GetParentProperty()
	{
		if (Parent)
		{
			UProperty* P = Parent->GetParentProperty();
			return P ? P : Property;
		}
		return Property;
	}

    void SetProperty( UProperty* Property, TreeItem* Offset, const TCHAR* Value )
	{
		guard(TreeItem::SetProperty);

		if (Class)
        {
            if (Cast<UClassProperty>(Property) && appStricmp(*Property->Category, TEXT("Drivers")) == 0)
            {
                // Save it.
                UClassProperty* ClassProp = CastChecked<UClassProperty>(Property);
                TArray<FRegistryObjectInfo> Classes;
                UObject::GetRegistryObjects(Classes, UClass::StaticClass(), ClassProp->MetaClass, 0);
                for (INT i = 0; i < Classes.Num(); i++)
                {
                    FString Path, Left, Right, Text;
                    Path = *Classes(i).Object;
                    if (Path.Split(TEXT("."), &Left, &Right))
                        Text = Localize(*Right, TEXT("ClassCaption"), *Left);
                    else
                        Text = Localize(TEXT("Language"), TEXT("Language"), TEXT("Core"), *Path);

                    if (appStricmp(*Text, Value) == 0)
                        GConfig->SetString(*FObjectPathName(Property->GetOwnerClass()), Property->GetName(), *Classes(i).Object);
                }
            }
            else
            {
                // To work with Array values.
                if (appStricmp(Value, TEXT("")) != 0)
                    Property->ImportText(Value, Offset->GetReadAddress(Property), PPF_Localized);
                if (Prefs.Immediate)
                {
                    guard(Immediate);
                    UProperty* ParentProp = Offset->GetParentProperty();

                    for (FObjectIterator It; It; ++It)
                    {
                        if (It->IsA(Class))
                        {
                            ParentProp->CopyCompleteValue(((BYTE*)* It) + ParentProp->Offset, GetReadAddress(ParentProp) + ParentProp->Offset);
                            It->PostEditChange();
                        }
                    }
                    unguard;
                }
                Class->GetDefaultObject()->SaveConfig();
            }
            return;
        }

		if( Parent )
			Parent->SetProperty( Property, Offset, Value );

		unguard;
	}

    void SetValue(FString InValue)
    {
        guard(TreeItem::SetValue);

        if (Property)
        {
            SetProperty( Property, this, *InValue );
        }

        unguard;
    }

    void Load()
    {
        guard(TreeItem::Load);

        if (Loaded) return;
        Loaded = true;

		TArray<FPreferencesInfo> NewPrefs;
		UObject::GetPreferences( NewPrefs, *Prefs.Caption, 0 );
		for( INT i=0; i<NewPrefs.Num(); i++ )
		{
            TreeItem Add(this, NewPrefs(i));
            Children.AddItem(Add);
		}

		LazyLoadClass();
        if (Class)
		{
            Class->GetDefaultObject()->LoadConfig(1);//!!
            for (TFieldIterator<UProperty> It(Class); It; ++It)
            {
                if
                    (((It->PropertyFlags & CPF_Config) == CPF_Config)
                        && (Class == It->GetOwnerClass() || !(It->PropertyFlags & CPF_GlobalConfig))
                        && (Prefs.Category == NAME_None || It->Category == Prefs.Category))
                    {
                        TreeItem Add(this, *It, *It->GetFName(), It->Offset, -1);
                        Children.AddItem(Add);
                    }
            }
		}

		if (Property)
        {
            UStructProperty* StructProperty;
            UArrayProperty* ArrayProperty;
            if (Property->ArrayDim > 1 && ArrayIndex == -1)
            {
                // Expand array.
                for (INT i = 0; i < Property->ArrayDim; i++)
                {
                    TreeItem Add(this, Property, Prefs.Caption, i * Property->ElementSize, i);
                    Children.AddItem(Add);
                }
            }
            else if ((ArrayProperty = Cast<UArrayProperty>(Property)) != NULL)
            {
                // Expand array.
                FArray* Array = GetArrayAddress();
                if (Array)
                    for (INT i = 0; i < Array->Num(); i++)
                    {
                        TreeItem Add(this, ArrayProperty->Inner, Prefs.Caption, i * ArrayProperty->Inner->ElementSize, i);
                        Children.AddItem(Add);
                    }
            }
            else if ((StructProperty = Cast<UStructProperty>(Property)) != NULL)
            {
                // Expand struct.
                for (TFieldIterator<UProperty> It(StructProperty->Struct); It; ++It)
                    if ((It->PropertyFlags & CPF_Config) == CPF_Config)
                    {
                        TreeItem Add(this, *It, *It->GetFName(), It->Offset, -1);
                        Children.AddItem(Add);
                    }
            }
            /* TODO
            else if (Cast<UObjectProperty>(Property) != NULL)
            {
                // Expand object properties.
                UObject** Object = (UObject * *)GetReadAddress(Property);
                if (Object)
                    Children.AddItem(new(TEXT("FCategoryItem"))FEditObjectItem(OwnerProperties, this, Object, Property));
            }
            */
        }

		unguard;
    }

    BOOL IsContainer()
    {
        return !Loaded || Children.Num() > 0;
    }

    void LazyLoadClass()
	{
		guard(TreeItem::LazyLoadClass);
		if (Prefs.Class == TEXT("")) return;
		if( !Class && !Failed )
		{
			Class = UObject::StaticLoadClass( UObject::StaticClass(), NULL, *Prefs.Class, NULL, LOAD_NoWarn, NULL );
			if( !Class )
			{
				Failed = 1;
				Prefs.Caption = FString::Printf( LocalizeError("FailedConfigLoad",TEXT("Window")), *Prefs.Class );
			}
		}
		unguard;
	}

	BYTE* GetReadAddress( UProperty* InProperty )
	{
		guard(TreeItem::GetReadAddress);

		if (Class)
            return &Class->Defaults(0);

        if (Property)
        {
            if (!Parent)
                return NULL;
            BYTE* AdrV = Parent->GetReadAddress(InProperty);
            if (!AdrV)
                return NULL;
            AdrV += Offset;
            if (Property && Property->IsA(UArrayProperty::StaticClass()))
                return (BYTE*)((FArray*)AdrV)->GetData();
            return AdrV;
        }

		return Parent ? Parent->GetReadAddress(InProperty) : NULL;
		unguard;
	}

    FArray* GetArrayAddress()
	{
		guard(TreeItem::GetArrayAddress);
		if (!Parent)
			return NULL;
		BYTE* AdrV = Parent->GetReadAddress(Property);
		if (!AdrV)
			return NULL;
		AdrV += Offset;
		return (FArray*)AdrV;
		unguard;
	}

    void GetStates( TArray<FName>& States )
	{
		guard(TreeItem::GetStates);
		if( Class )
			for( TFieldIterator<UState> StateIt(Class); StateIt; ++StateIt )
				if( StateIt->StateFlags & STATE_Editable )
					States.AddUniqueItem( StateIt->GetFName() );
		unguard;
	}
};

#define TYPE_TEXT 0
#define TYPE_LIST 1

class ItemValue
{
public:
    TreeItem* Data;
    wxArrayString Choices;
    wxString Value;
    INT Type;

    ItemValue(TreeItem* InData): Data(InData), Choices(), Type()
    {
        if (Data->Property)
        {
            UProperty* Property = Data->Property;
            if( Property->IsA(UBoolProperty::StaticClass()) )
            {
                AddChoice( GFalse );
                AddChoice( GTrue );
            }
            else if( Property->IsA(UByteProperty::StaticClass()) && Cast<UByteProperty>(Property)->Enum)
            {
                for( INT i=0; i<Cast<UByteProperty>(Property)->Enum->Names.Num(); i++ )
                    AddChoice( *Cast<UByteProperty>(Property)->Enum->Names(i) );
            }
            else if( Property->IsA(UNameProperty::StaticClass()) && Data->Prefs.Caption==NAME_InitialState )
            {
                TArray<FName> States;
                Data->GetStates( States );
                AddChoice( *FName(NAME_None) );
                for( INT i=0; i<States.Num(); i++ )
                    AddChoice( *States(i) );
            }
            else if( Cast<UClassProperty>(Property) && appStricmp(*Property->Category,TEXT("Drivers"))==0 )
            {
                UClassProperty* ClassProp = CastChecked<UClassProperty>(Property);
                TArray<FRegistryObjectInfo> Classes;
                UObject::GetRegistryObjects( Classes, UClass::StaticClass(), ClassProp->MetaClass, 0 );
                for( INT i=0; i<Classes.Num(); i++ )
                {
                    FString Path=Classes(i).Object, Left, Right;
                    if( Path.Split(TEXT("."),&Left,&Right) )
                        AddChoice( Localize(*Right,TEXT("ClassCaption"),*Left) );
                    else
                        AddChoice( Localize("Language","Language",TEXT("Core"),*Path) );
                }
            }
        }
        if (Choices.Count() > 0) Type = TYPE_LIST;
        CacheValue();
    }

    void AddChoice(wxString Str)
    {
        Choices.Add(Str);
    }

    void CacheValue()
    {
        Value = *Data->GetValue();
    }
};

static void PrefItemRenderer_OnEditingStarted( wxDataViewEvent &event )
{
	wxDataViewColumn* column = event.GetDataViewColumn();
	if (!column) return;
	wxDataViewRenderer* rend = column->GetRenderer();
	if (!rend) return;
	wxWindow* ed = rend->GetEditorCtrl();
	if (!ed) return;
	wxChoice* choice = wxDynamicCast(ed, wxChoice);
	if (!choice) return;
    wxUIActionSimulator sim;
    sim.Char(' '); // try open it by send space
}

static void PrefItemRenderer_OnChoice( wxCommandEvent &event )
{
	wxObject* obj = event.GetEventObject();
	if (!obj) return;
	wxChoice* choice = wxDynamicCast(obj, wxChoice);
	if (!choice) return;
	wxWindow* parent = choice->GetParent();
	if (!parent) return;
	wxDataViewCtrl* ctrl = wxDynamicCast(parent, wxDataViewCtrl);
	if (!ctrl) return;
	wxDataViewColumn* column = ctrl->GetColumn(1);
	if (!column) return;
	wxDataViewRenderer* rend = column->GetRenderer();
	if (!rend) return;
	rend->FinishEditing();
}

class PrefItemRenderer: public wxDataViewCustomRenderer
{
public:
    ItemValue* Item;
    ItemValue* EditItem;
    BOOL Binded;
    PrefItemRenderer() :
        wxDataViewCustomRenderer(wxT("void*"), wxDATAVIEW_CELL_EDITABLE),
		Item(NULL), EditItem(NULL), Binded(false)
    {
    }

    virtual bool HasEditorCtrl() const
    {
        return true;
    }

    virtual wxWindow* CreateEditorCtrl( wxWindow *parent, wxRect labelRect, const wxVariant &value )
    {
        EditItem = (ItemValue*)value.GetVoidPtr();

        if (EditItem->Type == TYPE_TEXT)
        {
            wxTextCtrl* ctrl = new wxTextCtrl(parent, wxID_ANY, EditItem->Value,
            	labelRect.GetPosition(),
				labelRect.GetSize(),
				wxTE_PROCESS_ENTER);
            ctrl->SetInsertionPointEnd();
            ctrl->SelectAll();

            return ctrl;
        }

		wxChoice* c = new wxChoice(parent->GetParent(), wxID_ANY,
				labelRect.GetPosition(),
				labelRect.GetSize(),
				EditItem->Choices); // XXX
		c->Reparent(parent);
		c->Move(labelRect.GetRight() - c->GetRect().width, wxDefaultCoord);
		c->SetStringSelection( EditItem->Value );
		c->Bind(wxEVT_CHOICE, &PrefItemRenderer_OnChoice, wxID_ANY);

		if (!Binded)
		{
			Binded = true;
			parent->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &PrefItemRenderer_OnEditingStarted, wxID_ANY);
		}

		return c;
    }

    virtual bool GetValueFromEditorCtrl( wxWindow* editor, wxVariant &value )
    {
        wxString s;
        if (EditItem->Type == TYPE_TEXT)
        {
            wxTextCtrl *text = (wxTextCtrl*) editor;
            s = text->GetValue();
        }
        else
        {
			wxChoice* c = (wxChoice*) editor;
			s = c->GetStringSelection();
        }
        EditItem->Value = s;
        value = EditItem;
        return true;
    }

    virtual bool Render( wxRect rect, wxDC *dc, int state )
    {
        RenderText( Item->Value, 0, rect, dc, state );
        return true;
    }

    virtual wxSize GetSize() const
    {
        wxSize sz;

        if (Item->Type == TYPE_TEXT)
        {
            sz.IncTo(GetTextExtent(Item->Value));
        }
        else
        {
            for ( wxArrayString::const_iterator i = Item->Choices.begin(); i != Item->Choices.end(); ++i )
            {
                sz.IncTo(GetTextExtent(*i));
            }

            // Allow some space for the right-side button, which is approximately the
            // size of a scrollbar (and getting pixel-exact value would be complicated).
            // Also add some whitespace between the text and the button:
            sz.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, m_editorCtrl);
            sz.x += GetTextExtent("M").x;
        }

        return sz;
    }

    virtual bool SetValue( const wxVariant &value )
    {
        Item = (ItemValue*)value.GetVoidPtr();
        return true;
    }

    virtual bool GetValue( wxVariant &value ) const
    {
        value = Item;
        return true;
    }
};

class PrefModel: public wxDataViewModel
{
public:
    TreeItem* Root;
    PrefModel(wxString title): wxDataViewModel()
    {
        Root = new TreeItem(title.t_str());
    }
    ~PrefModel() {}
    unsigned int GetColumnCount() const
    {
        return 2;
    }
    wxString GetColumnType(unsigned int column) const
    {
        return column == 0 ? "string" : "void*";
    }
    int Compare( const wxDataViewItem &item1, const wxDataViewItem &item2,
        unsigned int column, bool ascending ) const
    {
        TreeItem* data1 = GetData(ascending ? item1 : item2);
        TreeItem* data2 = GetData(ascending ? item2 : item1);

        if (data1->ArrayIndex != -1 && data2->ArrayIndex != -1 &&
        		data1->Parent == data2->Parent)
        	return data1->ArrayIndex - data2->ArrayIndex;

        int res = wxCmpNaturalGeneric(*data1->GetCaption(), *data2->GetCaption());
        if (res)
            return res;

        // items must be different
        return wxPtrToUInt(data1) - wxPtrToUInt(data2);
    }
    void GetValue(wxVariant& val, const wxDataViewItem& item, unsigned int column) const
    {
        TreeItem* data = GetData(item);
        if (column == 0)
            val = wxString(*data->GetCaption());
        else
            val = new ItemValue(data);
    }
    bool SetValue(const wxVariant& val, const wxDataViewItem& item, unsigned int column)
    {
        TreeItem* data = GetData(item);
        if (column == 1) {
            ItemValue* Item = (ItemValue*)val.GetVoidPtr();
            data->SetValue(Item->Value.t_str());
            Item->CacheValue();
        }
        return true;
    }
    wxDataViewItem GetParent(const wxDataViewItem& item) const
    {
        TreeItem* data = GetData(item);
        return wxDataViewItem(data->Parent == Root ? NULL : data->Parent);
    }
    bool IsContainer(const wxDataViewItem& item) const
    {
        TreeItem* data = GetData(item);
        return data->IsContainer();
    }
    unsigned GetChildren(const wxDataViewItem& item, wxDataViewItemArray& children) const
    {
        int cnt = 0;
        TreeItem* data = GetData(item);
        data->Load();
        INT i;
        for(i=0; i<data->Children.Num(); i++ )
		{
		    children.Add(wxDataViewItem(&data->Children(i)));
		}
        return i;
    }
    TreeItem* GetData(const wxDataViewItem& item) const
    {
        if (!item.IsOk()) return Root;
        return (TreeItem*)item.GetID();
    }
};

class wxFramePreferences : public wxFrame
{
    wxDataViewCtrl* dataView;
public:
	wxFramePreferences(wxString title, int xpos, int ypos, int width, int height)
		: wxFrame((wxFrame*)NULL, wxID_ANY, title, wxPoint(xpos, ypos), wxSize(width, height), wxCAPTION | wxMAXIMIZE_BOX | wxCLOSE_BOX | wxRESIZE_BORDER) // | wxFRAME_TOOL_WINDOW)
	{
		Connect(GetId(), wxEVT_CLOSE_WINDOW, wxCommandEventHandler(wxFramePreferences::OnClose));

		dataView = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(300, 300), wxDV_VARIABLE_LINE_HEIGHT);

        wxDataViewTextRenderer* rend0 = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_ACTIVATABLE);
        wxDataViewColumn* column0 = new wxDataViewColumn("", rend0, 0, GetSize().GetWidth()/2,
        		wxAlignment(wxALIGN_LEFT | wxALIGN_TOP), wxDATAVIEW_COL_RESIZABLE);
        dataView->AppendColumn(column0);
        dataView->SetExpanderColumn(column0);

        wxDataViewColumn* column1 = new wxDataViewColumn("", new PrefItemRenderer(), 1,
        		GetSize().GetWidth()/2, wxAlignment(wxALIGN_LEFT | wxALIGN_TOP), wxDATAVIEW_COL_RESIZABLE);
        dataView->AppendColumn(column1);

        PrefModel* prefModel = new PrefModel(title);
        dataView->AssociateModel(prefModel);

        column0->SetSortOrder(true);
	}

	void OnClose(wxCommandEvent& event)
	{
		Hide();
	}

	~wxFramePreferences()
	{

	}
};

/*-----------------------------------------------------------------------------
	Exec hook.
-----------------------------------------------------------------------------*/

// FExecHook.
class FExecHook : public FExec
{
private:
	wxFramePreferences* wxPreferences;
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar )
	{
		guard(FExecHook::Exec);
		if (ParseCommand(&Cmd, TEXT("pref")))
		{
			if (!wxPreferences) {
				wxPreferences = new wxFramePreferences(LocalizeGeneral("AdvancedOptionsTitle", TEXT("Window")), 100, 100, 500, 600);
				wxPreferences->Center();
			}

			if (GCurrentViewport && GCurrentViewport->IsFullscreen())
                GCurrentViewport->Exec(TEXT("EndFullscreen"), *GLog);

			wxPreferences->Iconize(false);
			wxPreferences->SetFocus();
			wxPreferences->Raise();
			wxPreferences->Show(true);

			return 1;
		}
		else if (ParseCommand(&Cmd, TEXT("GETSYSTEMINI")))
		{
			Ar.Logf(TEXT("%ls"), dynamic_cast<FConfigCacheIni*>(GConfig) ? *dynamic_cast<FConfigCacheIni*>(GConfig)->SystemIni : TEXT("UnrealTournament.ini"));
			return 1;
		}
		else if (ParseCommand(&Cmd, TEXT("GETUSERINI")))
		{
			Ar.Logf(TEXT("%ls"), dynamic_cast<FConfigCacheIni*>(GConfig) ? *dynamic_cast<FConfigCacheIni*>(GConfig)->UserIni : TEXT("User.ini"));
			return 1;
		}
		else if (ParseCommand(&Cmd, TEXT("RELAUNCHSUPPORT")))
		{
			Ar.Logf(TEXT("ENABLED"));
			return 1;
		}
		else if (ParseCommand(&Cmd, TEXT("LOGWINDOWSUPPORT")))
		{
			Ar.Logf(TEXT("ENABLED"));
			return 1;
		}
		else return 0;
		unguard;
	}
public:
    UEngine* Engine;
	FExecHook()
	: wxPreferences( NULL )
	{}
};
