// Copyright 2025-2026 NeoNexus Studios. All Rights Reserved.

#include "NeoReplicatedData.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"

// ------------------------------------------------------------------------------------------------
// FNeoDataEntry
// ------------------------------------------------------------------------------------------------

void FNeoDataEntry::PreReplicatedRemove(const FNeoDataMap& InArraySerializer) const
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->NotifyKeyRemoved(Key);
	}
}

void FNeoDataEntry::PostReplicatedAdd(const FNeoDataMap& InArraySerializer) const
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->NotifyKeyAdded(Key, Value);
	}
}

void FNeoDataEntry::PostReplicatedChange(const FNeoDataMap& InArraySerializer) const
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->NotifyKeyUpdated(Key, Value);
	}
}

// ------------------------------------------------------------------------------------------------
// FNeoDataMap
// ------------------------------------------------------------------------------------------------

void FNeoDataMap::AddOrUpdate(const FRecordKey& Key, const FRecordDefinition& Value)
{
	FNeoDataEntry* ExistingEntry = Items.FindByPredicate([&Key](const FNeoDataEntry& Entry)
	{
		return Entry.Key == Key;
	});

	if (ExistingEntry)
	{
		// Update
		ExistingEntry->Value = Value;
		MarkItemDirty(*ExistingEntry);
		
		// Notify Local
		if (Owner)
		{
			Owner->NotifyKeyUpdated(Key, Value);
		}
	}
	else
	{
		// Add
		FNeoDataEntry& NewEntry = Items.Add_GetRef(FNeoDataEntry(Key, Value));
		MarkItemDirty(NewEntry);
		
		// Notify Local
		if (Owner)
		{
			Owner->NotifyKeyAdded(Key, Value);
		}
	}
}

void FNeoDataMap::Remove(const FRecordKey& Key)
{
	for (auto It = Items.CreateIterator(); It; ++It)
	{
		if (It->Key == Key)
		{
			// Notify Local before removal
			if (Owner)
			{
				Owner->NotifyKeyRemoved(Key);
			}

			It.RemoveCurrent();
			MarkArrayDirty();
			return;
		}
	}
}

const FRecordDefinition* FNeoDataMap::Find(const FRecordKey& Key) const
{
	const FNeoDataEntry* Entry = Items.FindByPredicate([&Key](const FNeoDataEntry& Item)
	{
		return Item.Key == Key;
	});
	return Entry ? &Entry->Value : nullptr;
}

FRecordDefinition* FNeoDataMap::Find(const FRecordKey& Key)
{
	FNeoDataEntry* Entry = Items.FindByPredicate([&Key](const FNeoDataEntry& Item)
	{
		return Item.Key == Key;
	});
	return Entry ? &Entry->Value : nullptr;
}

// ------------------------------------------------------------------------------------------------
// UNeoReplicatedDataComponent
// ------------------------------------------------------------------------------------------------

UNeoReplicatedDataComponent::UNeoReplicatedDataComponent()
{
	SetIsReplicatedByDefault(true);
	DataMap.Owner = this;
}

void UNeoReplicatedDataComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNeoReplicatedDataComponent, DataMap);
}

void UNeoReplicatedDataComponent::SetData(const FRecordKey& Key, const FRecordDefinition& Value)
{
	// 1. Validate Key Type
	if (RestrictedKeyType)
	{
		const UScriptStruct* KeyStruct = Key.KeyData.GetScriptStruct();
		if (KeyStruct != RestrictedKeyType)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NeoDataSync] SetData Failed: Key type '%s' does not match RestrictedKeyType '%s' on Component '%s'"), 
				*GetNameSafe(KeyStruct), 
				*GetNameSafe(RestrictedKeyType),
				*GetNameSafe(this));
			return;
		}
	}

	// 2. Validate Value Type
	if (RestrictedValueType)
	{
		const UScriptStruct* ValueStruct = Value.Payload.GetScriptStruct();
		if (ValueStruct != RestrictedValueType)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NeoDataSync] SetData Failed: Value type '%s' does not match RestrictedValueType '%s' on Component '%s'"), 
				*GetNameSafe(ValueStruct), 
				*GetNameSafe(RestrictedValueType),
				*GetNameSafe(this));
			return;
		}
	}

	DataMap.AddOrUpdate(Key, Value);
}

void UNeoReplicatedDataComponent::RemoveData(const FRecordKey& Key)
{
	DataMap.Remove(Key);
}

bool UNeoReplicatedDataComponent::GetData(const FRecordKey& Key, FRecordDefinition& OutValue) const
{
	if (const FRecordDefinition* Found = DataMap.Find(Key))
	{
		OutValue = *Found;
		return true;
	}
	return false;
}

TArray<FRecordKey> UNeoReplicatedDataComponent::GetKeys() const
{
	TArray<FRecordKey> Keys;
	Keys.Reserve(DataMap.Items.Num());
	for (const FNeoDataEntry& Entry : DataMap.Items)
	{
		Keys.Add(Entry.Key);
	}
	return Keys;
}

void UNeoReplicatedDataComponent::NotifyKeyAdded(const FRecordKey& Key, const FRecordDefinition& Value) const
{
	OnKeyAdded.Broadcast(Key, Value);
}

void UNeoReplicatedDataComponent::NotifyKeyUpdated(const FRecordKey& Key, const FRecordDefinition& Value) const
{
	OnKeyUpdated.Broadcast(Key, Value);
}

void UNeoReplicatedDataComponent::NotifyKeyRemoved(const FRecordKey& Key) const
{
	OnKeyRemoved.Broadcast(Key);
}