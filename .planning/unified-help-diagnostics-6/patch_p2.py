import sys

# ═══ P2-5: upsertDescriptor performs all validations; applyKnowledge copies deprecation ═══
p = 'src/help/help_registry.cpp'
s = open(p, encoding='utf-8').read()
old = """bool HelpRegistry::upsertDescriptor( HelpDescriptor descriptor, QString *error )
{
    const bool existed = m_descriptors.contains( descriptor.id );
    // temporarily allow replace: registerDescriptor rejects duplicates, so
    // validate manually then insert/replace.
    auto fail = [error]( const QString &reason ) {
        if ( error )
            *error = reason;
        return false;
    };
    if ( !HelpId::isValid( descriptor.id ) )
        return fail( QStringLiteral( "invalid help id: %1" ).arg( descriptor.id ) );
    if ( !kindMatchesId( descriptor ) )
        return fail( QStringLiteral( "kind mismatch for id: %1" ).arg( descriptor.id ) );
    m_descriptors.insert( descriptor.id, std::move( descriptor ) );
    Q_UNUSED( existed );
    if ( error )
        error->clear();
    return true;
}"""
new = """bool HelpRegistry::upsertDescriptor( HelpDescriptor descriptor, QString *error )
{
    // Same validation as registerDescriptor, minus the duplicate rejection
    // (providers upsert derived descriptors over embedded knowledge).
    auto fail = [error]( const QString &reason ) {
        if ( error )
            *error = reason;
        return false;
    };
    if ( !HelpId::isValid( descriptor.id ) )
        return fail( QStringLiteral( "invalid help id: %1" ).arg( descriptor.id ) );
    if ( !kindMatchesId( descriptor ) )
        return fail( QStringLiteral( "kind mismatch for id: %1" ).arg( descriptor.id ) );
    if ( descriptor.deprecated && descriptor.supersededBy.isEmpty() )
        return fail( QStringLiteral( "deprecated descriptor without supersededBy: %1" ).arg( descriptor.id ) );
    if ( descriptor.id.startsWith( QLatin1String( "diagnostic." ) ) && !descriptor.diagnostic.has_value() )
        return fail( QStringLiteral( "diagnostic descriptor without DiagnosticInfo: %1" ).arg( descriptor.id ) );
    m_descriptors.insert( descriptor.id, std::move( descriptor ) );
    if ( error )
        error->clear();
    return true;
}"""
assert old in s, "upsert"
s = s.replace(old, new, 1)
open(p, 'w', encoding='utf-8', newline='\n').write(s)

# applyKnowledge (both providers) must preserve deprecation state
for p in ['src/help/command_help_provider.cpp', 'src/help/operator_help_provider.cpp']:
    s = open(p, encoding='utf-8').read()
    old2 = """    for ( const QString &doc : knowledge.docRefs ) {
        if ( !derived.docRefs.contains( doc ) )
            derived.docRefs << doc;
    }"""
    assert old2 in s, "docRefs anchor in " + p
    new2 = """    for ( const QString &doc : knowledge.docRefs ) {
        if ( !derived.docRefs.contains( doc ) )
            derived.docRefs << doc;
    }
    // deprecation is knowledge-level state: an entry marked deprecated in
    // content stays deprecated after the derived upsert
    if ( knowledge.deprecated ) {
        derived.deprecated = true;
        if ( !knowledge.supersededBy.isEmpty() )
            derived.supersededBy = knowledge.supersededBy;
    }"""
    s = s.replace(old2, new2, 1)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
print("P2-5 applied")
