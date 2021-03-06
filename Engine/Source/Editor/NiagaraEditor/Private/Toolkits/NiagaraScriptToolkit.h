// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Toolkits/IToolkitHost.h"
#include "Misc/NotifyHook.h"
#include "EditorUndoClient.h"
#include "AssetEditorToolkit.h"

class IDetailsView;
class SGraphEditor;
class UEdGraph;
class UNiagaraScript;
class UNiagaraScriptSource;
class FNiagaraScriptViewModel;
class FNiagaraObjectSelection;

/** Viewer/editor for a DataTable */
class FNiagaraScriptToolkit : public FAssetEditorToolkit
{
public:
	FNiagaraScriptToolkit();

	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

	/** Edits the specified Niagara Script */
	void Initialize( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraScript* Script );

	/** Destructor */
	virtual ~FNiagaraScriptToolkit();

	//~ Begin IToolkit Interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	//~ End IToolkit Interface

private:

	/** Spawns the tab with the update graph inside */
	TSharedRef<SDockTab> SpawnTabNodeGraph(const FSpawnTabArgs& Args);

	/** Spawns the tab with the script details inside. */
	TSharedRef<SDockTab> SpawnTabNodeDetails(const FSpawnTabArgs& Args);

	/** Spawns the tab with the script parameters inside. */
	TSharedRef<SDockTab> SpawnTabParameters(const FSpawnTabArgs& Args);

	/** Sets up commands for the toolkit toolbar. */
	void SetupCommands();

	FSlateIcon GetCompileStatusImage() const;
	FText GetCompileStatusTooltip() const;

	/** Builds the toolbar widget */
	void ExtendToolbar();

	/** Compiles the script. */
	void CompileScript();

	/** Refreshes the nodes in the script graph, updating the pins to match external changes. */
	void RefreshNodes();


	void OnExternalAssetSaved(const FString& Path, UObject* Object);

	FSlateIcon GetRefreshStatusImage() const;
	FText GetRefreshStatusTooltip() const;
	
private:

	/** The Script being edited */
	TSharedPtr<FNiagaraScriptViewModel> ScriptViewModel;

	/** The selection displayed by the details tab. */
	TSharedPtr<FNiagaraObjectSelection> DetailsSelection;

	/**	The tab ids for the Niagara editor */
	static const FName NodeGraphTabId; 
	static const FName DetailsTabId;
	static const FName ParametersTabId;

	FDelegateHandle PackageSavedDelegate;
	bool NeedsRefresh;
};
