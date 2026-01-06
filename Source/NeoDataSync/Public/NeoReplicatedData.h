// Copyright 2025-2026 NeoNexus Studios. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "InstancedStruct.h"
#include "StructUtils/InstancedStruct.h"

#include "NeoReplicatedData.generated.h"

struct FNeoDataMap;
class UNeoReplicatedDataComponent;

/**
 * Unique Identifier for a Record.
 * Acts as the Key in the Replicated Map.
 * Now supports any Struct via FInstancedStruct.
 */
USTRUCT(BlueprintType)
struct NEODATASYNC_API FRecordKey
{
	GENERATED_BODY()

	FRecordKey() {}
	FRecordKey(const FInstancedStruct& InKeyData) : KeyData(InKeyData) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync")
	FInstancedStruct KeyData;

	bool operator==(const FRecordKey& Other) const
	{
		// FInstancedStruct::operator== checks type and binary equality
		return KeyData == Other.KeyData;
	}

	friend uint32 GetTypeHash(const FRecordKey& Key)
	{
		if (Key.KeyData.IsValid())
		{
			// WARNING: MemCrc32 includes padding! Structs must be FMemory::Memzero'd before use.
			// Preferred: Use the struct's own hash function if available, or hash properties.
			const UScriptStruct* ScriptStruct = Key.KeyData.GetScriptStruct();
			
			// Note: This still risks padding issues but is the only generic options without reflection iteration.
			// Ideally: return ScriptStruct->GetStructTypeHash(Key.KeyData.GetMemory());
			return FCrc::MemCrc32(Key.KeyData.GetMemory(), ScriptStruct->GetStructureSize());
		}
		
		return 0;
	}
};

/**
 * Container for the Record Data.
 * Holds an arbitrary struct (Payload) that defines the record's schema.
 */
USTRUCT(BlueprintType)
struct NEODATASYNC_API FRecordDefinition
{
	GENERATED_BODY()

	FRecordDefinition() {}
	explicit FRecordDefinition(const FInstancedStruct& InPayload) : Payload(InPayload) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync")
	FInstancedStruct Payload;
};

// -------------------------------------------------------------------------
// Common Multi-Type Definitions ("Tuples")
// -------------------------------------------------------------------------

/** Definition for a String + Int pair */
USTRUCT(BlueprintType)
struct NEODATASYNC_API FNeoDataDefinition_SI
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync") FString StringValue;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync") int32 IntValue = 0;

	bool operator==(const FNeoDataDefinition_SI& Other) const {
		return StringValue == Other.StringValue && IntValue == Other.IntValue;
	}
	friend uint32 GetTypeHash(const FNeoDataDefinition_SI& Key) {
		return HashCombine(GetTypeHash(Key.StringValue), GetTypeHash(Key.IntValue));
	}
};

/** Definition for a String + Int + Float trio */
USTRUCT(BlueprintType)
struct NEODATASYNC_API FNeoDataDefinition_SIF
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync") FString StringValue;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync") int32 IntValue = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Sync") float FloatValue = 0.0f;

	bool operator==(const FNeoDataDefinition_SIF& Other) const {
		return StringValue == Other.StringValue && IntValue == Other.IntValue && FMath::IsNearlyEqual(FloatValue, Other.FloatValue);
	}
	friend uint32 GetTypeHash(const FNeoDataDefinition_SIF& Key) {
		return HashCombine(GetTypeHash(Key.StringValue), HashCombine(GetTypeHash(Key.IntValue), GetTypeHash(Key.FloatValue)));
	}
};

/**
 * A single entry in the replicated map.
 */
USTRUCT(BlueprintType)
struct NEODATASYNC_API FNeoDataEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FNeoDataEntry() {}
	FNeoDataEntry(const FRecordKey& InKey, const FRecordDefinition& InValue)
		: Key(InKey), Value(InValue) {}

	UPROPERTY()
	FRecordKey Key;

	UPROPERTY()
	FRecordDefinition Value;

	// Hooks for FastArraySerializer
	void PreReplicatedRemove(const FNeoDataMap& InArraySerializer) const;
	void PostReplicatedAdd(const FNeoDataMap& InArraySerializer) const;
	void PostReplicatedChange(const FNeoDataMap& InArraySerializer) const;

	bool operator==(const FNeoDataEntry& Other) const
	{
		return Key == Other.Key;
	}
};

/**
 * The Fast Array Serializer wrapper that behaves like a Map.
 */
USTRUCT(BlueprintType)
struct NEODATASYNC_API FNeoDataMap : public FFastArraySerializer
{
	GENERATED_BODY()

	FNeoDataMap() : Owner(nullptr) {}

	UPROPERTY()
	TArray<FNeoDataEntry> Items;

	// Owner back-reference for delegate broadcasting
	UPROPERTY(NotReplicated)
	TObjectPtr<UNeoReplicatedDataComponent> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FNeoDataEntry, FNeoDataMap>(Items, DeltaParms, *this);
	}

	void AddOrUpdate(const FRecordKey& Key, const FRecordDefinition& Value);
	void Remove(const FRecordKey& Key);
	const FRecordDefinition* Find(const FRecordKey& Key) const;
	FRecordDefinition* Find(const FRecordKey& Key);
};

/**
 * Trait to enable FastArraySerializer behavior
 */
template<>
struct TStructOpsTypeTraits<FNeoDataMap> : TStructOpsTypeTraitsBase2<FNeoDataMap>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNeoDataKeyChanged, const FRecordKey&, Key, const FRecordDefinition&, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNeoDataKeyRemoved, const FRecordKey&, Key);

/**
 * The Component container.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class NEODATASYNC_API UNeoReplicatedDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNeoReplicatedDataComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The Map
	UPROPERTY(Replicated)
	FNeoDataMap DataMap;

	// -------------------------------------------------------------------------
	// Schema Enforcement
	// -------------------------------------------------------------------------
	
	/** 
	 * Optional: Restrict Keys to this specific Struct type. 
	 * If set, SetData will ignore keys of other types.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NeoData|Schema")
	const UScriptStruct* RestrictedKeyType;

	/** 
	 * Optional: Restrict Values to this specific Struct type. 
	 * If set, SetData will ignore values of other types.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NeoData|Schema")
	const UScriptStruct* RestrictedValueType;

	// -------------------------------------------------------------------------
	// Blueprint API
	// -------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NeoData")
	void SetData(const FRecordKey& Key, const FRecordDefinition& Value);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NeoData")
	void RemoveData(const FRecordKey& Key);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NeoData")
	bool GetData(const FRecordKey& Key, FRecordDefinition& OutValue) const;
	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NeoData")
	TArray<FRecordKey> GetKeys() const;

	// -------------------------------------------------------------------------
	// C++ Templated API
	// -------------------------------------------------------------------------

	/**
	 * Strongly typed Set for C++.
	 * Automatically wraps structs in FRecordKey/FRecordDefinition.
	 * Usage: MyComponent->SetTypedData(MyKeyStruct, MyValueStruct);
	 */
	template <typename KeyT, typename ValueT>
	void SetTypedData(const KeyT& InKey, const ValueT& InValue)
	{
		// Static checks to ensure we are passing valid UStructs
		static_assert(TModels<CStaticStructProvider, KeyT>::Value, "KeyT must be a USTRUCT");
		static_assert(TModels<CStaticStructProvider, ValueT>::Value, "ValueT must be a USTRUCT");

		FRecordKey WrappedKey(FInstancedStruct::Make(InKey));
		FRecordDefinition WrappedValue(FInstancedStruct::Make(InValue));

		SetData(WrappedKey, WrappedValue);
	}

	/**
	 * Strongly typed Get for C++.
	 * Usage: 
	 *    FMyValue Val;
	 *    if (MyComponent->GetTypedData(MyKey, Val)) { ... }
	 */
	template <typename KeyT, typename ValueT>
	bool GetTypedData(const KeyT& InKey, ValueT& OutValue) const
	{
		static_assert(TModels<CStaticStructProvider, KeyT>::Value, "KeyT must be a USTRUCT");
		static_assert(TModels<CStaticStructProvider, ValueT>::Value, "ValueT must be a USTRUCT");

		FRecordKey WrappedKey(FInstancedStruct::Make(InKey));
		FRecordDefinition OutDef;

		if (GetData(WrappedKey, OutDef))
		{
			if (const ValueT* Ptr = OutDef.Payload.GetPtr<ValueT>())
			{
				OutValue = *Ptr;
				return true;
			}
		}
		return false;
	}

	// Delegates
	UPROPERTY(BlueprintAssignable, Category = "NeoData")
	FOnNeoDataKeyChanged OnKeyAdded;

	UPROPERTY(BlueprintAssignable, Category = "NeoData")
	FOnNeoDataKeyChanged OnKeyUpdated;

	UPROPERTY(BlueprintAssignable, Category = "NeoData")
	FOnNeoDataKeyRemoved OnKeyRemoved;

	// Internal hook for the struct to call back
	void NotifyKeyAdded(const FRecordKey& Key, const FRecordDefinition& Value) const;
	void NotifyKeyUpdated(const FRecordKey& Key, const FRecordDefinition& Value) const;
	void NotifyKeyRemoved(const FRecordKey& Key) const;
};