#include "MiscUtil.h"

#include <sstream>
#include <set>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>

#include "skse64/GameAPI.h"
#include "skse64/GameCamera.h"
#include "skse64/GameReferences.h"
#include "skse64/GameSettings.h"
#include "skse64/GameMenus.h"
//#include "skse64/SafeWrite.h"

#include "skse64/GameRTTI.h"
#include "skse64/GameTypes.h"
#include "skse64/PapyrusArgs.h"


//#include "SafeRead.h"

// TODO: test TESTScanCellNPCsByFaction and remove prepend

namespace MiscUtil {

	typedef void(*_ToggleFreeCam)(uintptr_t addr, bool stopTime);
	void ToggleFreeCamera(StaticFunctionTag* base, bool arg1) {
		int stopTime = arg1 ? 1 : 0;
		RelocAddr<uintptr_t> g_freeCam(0x02EDEF38);
		RelocAddr<_ToggleFreeCam> ToggleFreeCam(0x00849DC0);
		ToggleFreeCam(g_freeCam.GetUIntPtr(), stopTime);
	}

	extern RelocPtr<SettingCollectionList*> g_iniSettingCollection;
	void SetFreeCameraSpeed(StaticFunctionTag* base, float speed) {
		Setting * setting = (*g_iniSettingCollection)->Get("fFreeCameraTranslationSpeed:Camera");
		if (setting)
			setting->SetDouble(speed);
	}

	void SetFreeCameraState(StaticFunctionTag* base, bool enable, float speed) {
		PlayerCamera* pc = PlayerCamera::GetSingleton();
		if (pc) {
			bool InFreeCamera = pc->cameraState->stateId == pc->kCameraState_Free;
			// Leave free camera
			if (InFreeCamera && !enable)
				ToggleFreeCamera(NULL, false);
			// Enter free camera
			else if (!InFreeCamera && enable){
				//SetFreeCameraSpeed(NULL, speed);
				ToggleFreeCamera(NULL, false);
				//CALL_MEMBER_FN(pc, SetCameraState)(pc->cameraStates[pc->kCameraState_Free]);
			}
		}
	}
	
	void PrintConsole(StaticFunctionTag* base, BSFixedString text) {
		if (!text.data)
			return;
		else if (strlen(text.data) < 1024)
			Console_Print(text.data);
		else { // Large strings printed to console crash the game - truncate it
			std::string msg = text.data;
			msg.resize(1020);
			msg.append("...");
			Console_Print(msg.c_str());
		}
	}

	/*void SetMenus(StaticFunctionTag* base, bool enabled){
		UInt32 ptr = Lib::SafeRead32(0x12E3548);
		if (ptr != 0) SafeWrite8(ptr + 0x118, (enabled ? 1 : 0));
	}*/
	void SetMenus(StaticFunctionTag* base, bool enabled){
		MenuManager::GetSingleton()->unk_1C0 = enabled;
	}

	BSFixedString GetRaceEditorID(StaticFunctionTag* base, TESRace* RaceRef) {
		return RaceRef ? RaceRef->editorId.data : NULL;
	}

	BSFixedString GetActorRaceEditorID(StaticFunctionTag* base, Actor* ActorRef) {
		return ActorRef ? ActorRef->race->editorId.data : NULL;
	}



	/*struct CellLocker
	{
		CellLocker(TESObjectCELL* cell)
		{
			locked = cell;
			if (cell != NULL)
			{
				static int _lockCellEnterHelper = 0x4C0180;
				_asm
				{
					pushad
					pushfd

						mov ecx, cell
						call _lockCellEnterHelper

						popfd
						popad
				};
			}
		}

		~CellLocker()
		{
			TESObjectCELL * cell = locked;
			if (cell != NULL)
			{
				static int _lockCellExitHelper = 0x4C0190;
				_asm
				{
					pushad
					pushfd

						mov ecx, cell
						call _lockCellExitHelper

						popfd
						popad
				}
			}
		}

	private:
		TESObjectCELL * locked;
	};*/

	inline bool HasKeyword(TESObjectREFR* ObjRef, BGSKeyword* findKeyword) {
		BGSKeywordForm* keywords = DYNAMIC_CAST(ObjRef, TESForm, BGSKeywordForm);
		if (keywords) return keywords->HasKeyword(findKeyword);
		else return false;
	}

	bool IsWithinRadius(TESObjectREFR* CenterObj, TESObjectREFR* ObjRef, float distance) {
		NiPoint3 &a = CenterObj->pos;
		NiPoint3 &b = ObjRef->pos;
		float tempx, tempy, tempz, tempd;
		tempx = std::abs(a.x - b.x);
		tempy = std::abs(a.y - b.y);
		tempz = std::abs(a.z - b.z);
		if (tempx + tempy + tempz < distance)
			return true; // very small distances
		if (tempx + tempy + tempz > distance / 2)
			return false; // very large distances
		tempx = tempx * tempx;
		tempy = tempy * tempy;
		tempz = tempz * tempz;
		tempd = distance * distance;
		if (tempx + tempy + tempz < tempd)
			return true; // near but within distance
		return false;
	}

	VMResultArray<TESObjectREFR*> ScanCellObjects(StaticFunctionTag* base, UInt32 FormType, TESObjectREFR* CenterObj, float SearchRadius, BGSKeyword* FindKeyword) {
		VMResultArray<TESObjectREFR*> output;
		if (CenterObj != NULL) {
			_MESSAGE("Cell scanning from form: %lu", CenterObj->formID);
			TESObjectCELL* Cell = CenterObj->parentCell;
			if (Cell) {
				//CellLocker _locker(Cell);
				tArray<TESObjectREFR*> objList = Cell->objectList;
				UInt32 count = objList.count;
				_MESSAGE("\tcell item count: %lu", count);
				for (UInt32 idx = 0; idx < count; ++idx) {
					TESObjectREFR* ObjRef = objList[idx];
					if (ObjRef == NULL || ObjRef->baseForm->GetFormType() != FormType) continue;
					else if (SearchRadius > 0.0f && !IsWithinRadius(CenterObj, ObjRef, SearchRadius)) continue;
					else if (FindKeyword && !HasKeyword(ObjRef, FindKeyword)) continue;
					else output.push_back(ObjRef);
				}
			}
			_MESSAGE("Cell scanning found: %d", (int)output.size());
		}
		return output;
	}


	VMResultArray<Actor*> ScanCellNPCs(StaticFunctionTag* base, TESObjectREFR* CenterObj, float SearchRadius, BGSKeyword* FindKeyword, bool ignoredead) {
		VMResultArray<Actor*> output;
		if (CenterObj != NULL) {
			_MESSAGE("Cell scanning from form: %lu", CenterObj->formID);
			TESObjectCELL* Cell = CenterObj->parentCell;
			if (Cell) {
				//CellLocker _locker(Cell);
				tArray<TESObjectREFR*> objList = Cell->objectList;
				UInt32 count = objList.count;
				_MESSAGE("\tcell item count: %lu", count);
				for (UInt32 idx = 0; idx < count; ++idx) {
					TESObjectREFR* ObjRef = objList[idx];
					Actor *ActorRef = ObjRef != NULL ? DYNAMIC_CAST(ObjRef, TESObjectREFR, Actor) : NULL;
					if (ActorRef == NULL || (ignoredead && ActorRef->IsDead(1))) continue;
					else if (SearchRadius > 0.0f && !IsWithinRadius(CenterObj, ActorRef, SearchRadius)) continue;
					else if (FindKeyword && !HasKeyword(ActorRef, FindKeyword)) continue;
					else output.push_back(ActorRef);
				}
			}
			_MESSAGE("Cell scanning found: %d", (int)output.size());
		}
		return output;
	}




	// Copied from skse/PapyrusActor.cpp
	typedef std::set<TESFaction*> FactionRankSet;
	class CollectUniqueFactions : public Actor::FactionVisitor
	{
	public:
		CollectUniqueFactions::CollectUniqueFactions(FactionRankSet * rankSet, SInt8 min, SInt8 max, TESFaction* FactionRef) : m_rankSet(rankSet), m_min(min), m_max(max), FindFaction(FactionRef) { }
		virtual bool Accept(TESFaction * faction, SInt8 rank)
		{
			if (rank >= m_min && rank <= m_max && FindFaction->formID == faction->formID)
				m_rankSet->insert(faction);
			return false;
		}

	private:
		SInt8 m_min;
		SInt8 m_max;
		FactionRankSet * m_rankSet;
		TESFaction* FindFaction;
	};
	// copied end

	bool IsInFaction(Actor* ActorRef, TESFaction* FactionRef, SInt8 min, SInt8 max){
		if (ActorRef != NULL && FactionRef != NULL){
			FactionRankSet rankSet;
			CollectUniqueFactions factionVisitor(&rankSet, min, max, FactionRef);
			ActorRef->VisitFactions(factionVisitor);
			if (rankSet.size() > 0)
				return true;
		}
		return false;
	}


	VMResultArray<Actor*> ScanCellNPCsByFaction(StaticFunctionTag* base, TESFaction* FindFaction, TESObjectREFR* CenterObj, float SearchRadius, SInt32 gte, SInt32 lte, bool ignoredead) {
		VMResultArray<Actor*> output;
		if (CenterObj != NULL && FindFaction != NULL) {

			if (gte > SCHAR_MAX)
				gte = SCHAR_MAX;
			if (gte < SCHAR_MIN)
				gte = SCHAR_MIN;
			if (lte < SCHAR_MIN)
				lte = SCHAR_MIN;
			if (lte > SCHAR_MAX)
				lte = SCHAR_MAX;

			_MESSAGE("Cell scanning for faction(%lu, %d, %d) from form: %lu", FindFaction->formID, (int)gte, (int)lte, CenterObj->formID);
			TESObjectCELL* Cell = CenterObj->parentCell;
			if (Cell) {
				//CellLocker _locker(Cell);
				tArray<TESObjectREFR*> objList = Cell->objectList;
				UInt32 count = objList.count;
				_MESSAGE("\tcell item count: %lu", count);
				for (UInt32 idx = 0; idx < count; ++idx) {
					TESObjectREFR* ObjRef = objList[idx];
					Actor *ActorRef = ObjRef != NULL ? DYNAMIC_CAST(ObjRef, TESObjectREFR, Actor) : NULL;
					if (ActorRef == NULL || (ignoredead && ActorRef->IsDead(1))) continue;
					else if (SearchRadius > 0.0f && !IsWithinRadius(CenterObj, ActorRef, SearchRadius)) continue;
					else if (!IsInFaction(ActorRef, FindFaction, gte, lte)) continue;
					else output.push_back(ActorRef);
				}
			}
			_MESSAGE("Cell scanning found: %d", (int)output.size());
		}
		return output;
	}


	namespace fs = boost::filesystem;

	bool FileExists(StaticFunctionTag* base, BSFixedString name) {
		if (!name.data || name.data[0] == '\0') return false;
		fs::path Path = fs::path(name.data);
		return fs::exists(Path);
	}

	VMResultArray<BSFixedString> FilesInFolder(StaticFunctionTag* base, BSFixedString dirpath, BSFixedString extension) {
		VMResultArray<BSFixedString> arr;
		if (dirpath.data && dirpath.data[0] != '\0') {
			fs::path dir(dirpath.data);
			fs::directory_iterator end_iter;
			if (fs::exists(dir) && fs::is_directory(dir)) {
				std::string ext;
				if (extension.data[0] == '.') ext = extension.data;
				else {
					ext = ".";
					ext.append(extension.data);
				}
				_MESSAGE("dir: %s ext: %s", dirpath.data, ext.c_str());
				for (fs::directory_iterator dir_iter(dir); dir_iter != end_iter; ++dir_iter) {
					if (fs::is_regular_file(dir_iter->status())) {
						fs::path filepath = dir_iter->path();
						std::string file = filepath.filename().generic_string();
						//std::string ext = filepath.extension().generic_string();
						//_MESSAGE("file: %s ext: %s", file.c_str(), ext.c_str());
						if (ext == "*" || boost::iequals(filepath.extension().generic_string(), ext))
							arr.push_back(BSFixedString(file.c_str()));
					}
				}
			}
		}
		return arr;
	}

	BSFixedString ReadFromFile(StaticFunctionTag* base, BSFixedString filename) {
		if (!filename.data || filename.data[0] == '\0') return BSFixedString("");
		std::string output = "0";
		fs::path File(filename.data);
		if (fs::exists(File) && !fs::is_directory(File)) {
			fs::ifstream doc;
			try
			{
				doc.open(File, std::ios::in | std::ios::binary);
				if (!doc.fail()) {
					std::stringstream ss;
					ss << doc.rdbuf();
					output = ss.str();
				}
			}
			catch (std::exception&)
			{
				_MESSAGE("Failed to load/read file: %s", File.generic_string().c_str());
			}
			if (doc.is_open())
				doc.close();

		}
		return BSFixedString(output.c_str());
	}

	bool WriteToFile(StaticFunctionTag* base, BSFixedString filename, BSFixedString input, bool append, bool timestamp) {
		if (!filename.data || filename.data[0] == '\0') return false;
		bool output = false;
		fs::path File(filename.data);
		fs::ofstream doc;
		try
		{
			if(append) doc.open(File, std::ios::out | std::ios::app | std::ios::binary);
			else doc.open(File, std::ios::out | std::ios::binary);
			if (doc.is_open()) {
				doc << input.data;
				doc.close();
				output = true;
			}
		}
		catch (std::exception&)
		{
			_MESSAGE("Failed to load/write file: %s", File.generic_string().c_str());
		}
		if (doc.is_open()) doc.close();
		return output;
	}

} // MiscUtil


#include "skse64/PapyrusNativeFunctions.h"

void MiscUtil::RegisterFuncs(VMClassRegistry* registry) {
	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, bool>("ToggleFreeCamera", "MiscUtil", ToggleFreeCamera, registry));
	registry->SetFunctionFlags("MiscUtil", "ToggleFreeCamera", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, float>("SetFreeCameraSpeed", "MiscUtil", SetFreeCameraSpeed, registry));
	registry->SetFunctionFlags("MiscUtil", "SetFreeCameraSpeed", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction2<StaticFunctionTag, void, bool, float>("SetFreeCameraState", "MiscUtil", SetFreeCameraState, registry));
	registry->SetFunctionFlags("MiscUtil", "SetFreeCameraState", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, BSFixedString>("PrintConsole", "MiscUtil", PrintConsole, registry));
	registry->SetFunctionFlags("MiscUtil", "PrintConsole", VMClassRegistry::kFunctionFlag_NoWait);

	//registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, bool>("SetMenus", "MiscUtil", SetMenus, registry));
	//registry->SetFunctionFlags("MiscUtil", "SetMenus", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, TESRace*>("GetRaceEditorID", "MiscUtil", GetRaceEditorID, registry));
	registry->SetFunctionFlags("MiscUtil", "GetRaceEditorID", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, Actor*>("GetActorRaceEditorID", "MiscUtil", GetActorRaceEditorID, registry));
	registry->SetFunctionFlags("MiscUtil", "GetActorRaceEditorID", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, bool, BSFixedString>("FileExists", "MiscUtil", FileExists, registry));
	registry->SetFunctionFlags("MiscUtil", "FileExists", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction4<StaticFunctionTag, VMResultArray<TESObjectREFR*>, UInt32, TESObjectREFR*, float, BGSKeyword*>("ScanCellObjects", "MiscUtil", ScanCellObjects, registry));
	registry->RegisterFunction(new NativeFunction4<StaticFunctionTag, VMResultArray<Actor*>, TESObjectREFR*, float, BGSKeyword*, bool>("ScanCellNPCs", "MiscUtil", ScanCellNPCs, registry));
	registry->RegisterFunction(new NativeFunction6<StaticFunctionTag, VMResultArray<Actor*>, TESFaction*, TESObjectREFR*, float, SInt32, SInt32, bool>("TESTScanCellNPCsByFaction", "MiscUtil", ScanCellNPCsByFaction, registry));

	registry->RegisterFunction(new NativeFunction2<StaticFunctionTag, VMResultArray<BSFixedString>, BSFixedString, BSFixedString>("FilesInFolder", "MiscUtil", FilesInFolder, registry));
	registry->SetFunctionFlags("MiscUtil", "FilesInFolder", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, BSFixedString>("ReadFromFile", "MiscUtil", ReadFromFile, registry));
	registry->RegisterFunction(new NativeFunction4<StaticFunctionTag, bool, BSFixedString, BSFixedString, bool, bool>("WriteToFile", "MiscUtil", WriteToFile, registry));

}


// These functions are mostly unused and serve little use beyond major security risks.

//#include <iostream>
//#include <fstream>
//#include <ctime>

//#include "skse64/NiNodes.h"
//#include "skse64/NiGeometry.h"
//#include "skse64/GameData.h"
//#include "skse64/GameTypes.h"
//#include "skse64/GameForms.h"
//#include "skse64/GameRTTI.h"

//#pragma warning(disable: 4996)
//#pragma warning(disable: 4229)

/*namespace MiscUtil{


	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, BSFixedString>("ExecuteBat", "MiscUtil", ExecuteBat, registry));
	registry->SetFunctionFlags("MiscUtil", "ExecuteBat", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, BSFixedString>("ReadFromFile", "MiscUtil", ReadFromFile, registry));
	registry->SetFunctionFlags("MiscUtil", "ReadFromFile", VMClassRegistry::kFunctionFlag_NoWait);

	registry->RegisterFunction(new NativeFunction4<StaticFunctionTag, bool, BSFixedString, BSFixedString, bool, bool>("WriteToFile", "MiscUtil", WriteToFile, registry));
	registry->SetFunctionFlags("MiscUtil", "WriteToFile", VMClassRegistry::kFunctionFlag_NoWait);

	BSFixedString ReadFromFile(StaticFunctionTag* base, BSFixedString fileName) {
		std::ifstream doc;
		try {
			doc.open(fileName.data, std::ifstream::in | std::ifstream::binary);
		}
		catch (std::exception*){
			return NULL;
		}
		if (doc.fail())
			return NULL;

		std::string contents;
		doc.seekg(0, std::ios::end);
		contents.resize(doc.tellg());
		doc.seekg(0, std::ios::beg);
		doc.read(&contents[0], contents.size());
		doc.close();
		return BSFixedString(contents.c_str());
	}

	bool WriteToFile(StaticFunctionTag* base, BSFixedString fileName, BSFixedString text, bool append, bool timestamp) {
		std::ofstream doc;
		try {
			doc.open(fileName.data, append ? (std::ofstream::out | std::ofstream::app) : std::ofstream::out);
		}
		catch (std::exception*) {
			return false;
		}
		if (doc.fail())
			return false;

		if (timestamp) {
			time_t curTime;
			time(&curTime);
			struct tm * timeinfo = localtime(&curTime);
			doc << "[" << asctime(timeinfo) << "] ";
		}

		doc << text.data;
		doc.close();
		return true;
	}


	float GetNodeRotation(StaticFunctionTag* base, TESObjectREFR* obj, BSFixedString nodeName, bool firstPerson, UInt32 nth) {
		return 0.0f;

		if (nth < 0 || nth > 8 || !obj)
			return -1.0f;

		NiAVObject* skeleton = obj->GetNiNode();
		PlayerCharacter* player = DYNAMIC_CAST(obj, TESObjectREFR, PlayerCharacter);
		if (player && player->loadedState)
			skeleton = firstPerson ? player->firstPersonSkeleton : player->loadedState->node;
		if (!skeleton)
			return -1.0f;

		NiAVObject* node;
		if (skeleton && nodeName.data[0])
			node = skeleton->GetObjectByName(&nodeName.data);
		if (!node)
			return -1.0f;

		return *node->m_worldTransform.rot.data[nth];
	}

	int batAddr = 0;
	int overBat = 0;
	char textBat[256];
	typedef char(*__cdecl batCommand)(int, char*, int, int, int, int*, int, int);
	void ExecuteBat(StaticFunctionTag* base, BSFixedString fileName) {
		memset(textBat, 0, sizeof(char)* 256);
		memcpy(textBat, fileName.data, strlen(fileName.data));

		batAddr = (int)textBat;
		overBat = 1;

		batCommand func = (batCommand)0x547AA0;
		func(0, 0, 0, 0, 0, 0, 0, 0);
	}

	int jumpBat = 0x547AAE;
	int jumpBat2 = 0x547AEF;
	void InitPlugin(){
		void * batCodeStart;
		_asm
		{
			mov batCodeStart, offset batStart
				jmp batEnd
			batStart :
			sub esp, 0x104
				cmp overBat, 0
				je batOrig
				mov overBat, 0
				mov eax, batAddr
				mov ecx, 0
			batCopy:
			mov dl, [eax + ecx]
				mov[esp + ecx], dl
				cmp dl, 0
				je batFinish
				inc ecx
				jmp batCopy
			batFinish :
			jmp jumpBat2
			batOrig :
			jmp jumpBat
			batEnd :
		}

		WriteRelJump(0x547AA8, (UInt32)batCodeStart);
	}

}

*/