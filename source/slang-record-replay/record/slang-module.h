#ifndef SLANG_MODULE_H
#define SLANG_MODULE_H

#include "slang-com-ptr.h"
#include "slang.h"
#include "slang-com-helper.h"
#include "../../core/slang-smart-pointer.h"
#include "../../slang/slang-compiler.h"
#include "slang-entrypoint.h"
#include "record-manager.h"

namespace SlangRecord
{
    using namespace Slang;
    class ModuleRecorder : public slang::IModule, public RefObject
    {
    public:
        SLANG_COM_INTERFACE(0xb1802991, 0x185a, 0x4a03, { 0xa7, 0x7e, 0x0c, 0x86, 0xe0, 0x68, 0x2a, 0xab })

        SLANG_REF_OBJECT_IUNKNOWN_ALL
        ISlangUnknown* getInterface(const Guid& guid);

        explicit ModuleRecorder(slang::IModule* module, RecordManager* recordManager);

        // Interfaces for `IModule`
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL findEntryPointByName(
            char const* name, slang::IEntryPoint** outEntryPoint) override;
        virtual SLANG_NO_THROW SlangInt32 SLANG_MCALL getDefinedEntryPointCount() override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL
            getDefinedEntryPoint(SlangInt32 index, slang::IEntryPoint** outEntryPoint) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL serialize(ISlangBlob** outSerializedBlob) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL writeToFile(char const* fileName) override;
        virtual SLANG_NO_THROW const char* SLANG_MCALL getName() override;
        virtual SLANG_NO_THROW const char* SLANG_MCALL getFilePath() override;
        virtual SLANG_NO_THROW const char* SLANG_MCALL getUniqueIdentity() override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL findAndCheckEntryPoint(
            char const* name, SlangStage stage, slang::IEntryPoint** outEntryPoint, ISlangBlob** outDiagnostics) override;
        virtual SLANG_NO_THROW SlangInt32 SLANG_MCALL getDependencyFileCount() override;
        virtual SLANG_NO_THROW char const* SLANG_MCALL getDependencyFilePath(
            SlangInt32 index) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL precompileForTarget(
            SlangCompileTarget target,
            ISlangBlob** outDiagnostics) override;

        // Interfaces for `IComponentType`
        virtual SLANG_NO_THROW slang::ISession* SLANG_MCALL getSession() override;
        virtual SLANG_NO_THROW slang::ProgramLayout* SLANG_MCALL getLayout(
            SlangInt    targetIndex = 0,
            slang::IBlob**     outDiagnostics = nullptr) override;
        virtual SLANG_NO_THROW SlangInt SLANG_MCALL getSpecializationParamCount() override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL getEntryPointCode(
            SlangInt    entryPointIndex,
            SlangInt    targetIndex,
            slang::IBlob**     outCode,
            slang::IBlob**     outDiagnostics = nullptr) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL getTargetCode(
            SlangInt    targetIndex,
            slang::IBlob** outCode,
            slang::IBlob** outDiagnostics = nullptr) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL getResultAsFileSystem(
            SlangInt    entryPointIndex,
            SlangInt    targetIndex,
            ISlangMutableFileSystem** outFileSystem) override;
        virtual SLANG_NO_THROW void SLANG_MCALL getEntryPointHash(
            SlangInt    entryPointIndex,
            SlangInt    targetIndex,
            slang::IBlob**     outHash) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL specialize(
            slang::SpecializationArg const*    specializationArgs,
            SlangInt                    specializationArgCount,
            slang::IComponentType**            outSpecializedComponentType,
            ISlangBlob**                outDiagnostics = nullptr) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL link(
            slang::IComponentType**            outLinkedComponentType,
            ISlangBlob**                outDiagnostics = nullptr) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL getEntryPointHostCallable(
            int                     entryPointIndex,
            int                     targetIndex,
            ISlangSharedLibrary**   outSharedLibrary,
            slang::IBlob**          outDiagnostics = 0) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL renameEntryPoint(
            const char* newName, IComponentType** outEntryPoint) override;
        virtual SLANG_NO_THROW SlangResult SLANG_MCALL linkWithOptions(
            IComponentType** outLinkedComponentType,
            uint32_t compilerOptionEntryCount,
            slang::CompilerOptionEntry* compilerOptionEntries,
            ISlangBlob** outDiagnostics = nullptr) override;
        virtual SLANG_NO_THROW slang::DeclReflection* getModuleReflection() override;

        slang::IModule* getActualModule() const { return m_actualModule; }
    private:
        EntryPointRecorder* getEntryPointRecorder(slang::IEntryPoint* entryPoint);
        Slang::ComPtr<slang::IModule> m_actualModule;
        uint64_t                      m_moduleHandle = 0;
        RecordManager*               m_recordManager = nullptr;

        // `IEntryPoint` can only be created from 'IModule', so we need to record it in
        // this class, and create a map such that we don't create new `EntryPointRecorder`
        // for the same `IEntryPoint`.
        Dictionary<slang::IEntryPoint*, EntryPointRecorder*> m_mapEntryPointToRecord;
        List<ComPtr<EntryPointRecorder>> m_entryPointsRecordAllocation;
    };
} // namespace SlangRecord

#endif // SLANG_MODULE_H
