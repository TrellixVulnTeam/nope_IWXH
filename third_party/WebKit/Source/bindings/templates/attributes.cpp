{% from 'conversions.cpp' import v8_value_to_local_cpp_value %}


{##############################################################################}
{% macro attribute_getter(attribute, world_suffix) %}
{% filter conditional(attribute.conditional_string) %}
static void {{attribute.name}}AttributeGetter{{world_suffix}}(
{%- if attribute.is_expose_js_accessors %}
const v8::FunctionCallbackInfo<v8::Value>& info
{%- else %}
const v8::PropertyCallbackInfo<v8::Value>& info
{%- endif %})
{
    {% if attribute.is_reflect and not attribute.is_url
          and attribute.idl_type == 'DOMString' and is_node
          and not attribute.is_implemented_in_private_script %}
    {% set cpp_class, v8_class = 'Element', 'V8Element' %}
    {% endif %}
    {# holder #}
    {% if not attribute.is_static %}
    v8::Local<v8::Object> holder = info.Holder();
    {% endif %}
    {# impl #}
    {% if attribute.cached_attribute_validation_method %}
    v8::Local<v8::String> propertyName = v8AtomicString(info.GetIsolate(), "{{attribute.name}}");
    {{cpp_class}}* impl = {{v8_class}}::toImpl(holder);
    if (!impl->{{attribute.cached_attribute_validation_method}}()) {
        v8::Local<v8::Value> v8Value = V8HiddenValue::getHiddenValue(info.GetIsolate(), holder, propertyName);
        if (!v8Value.IsEmpty()) {
            v8SetReturnValue(info, v8Value);
            return;
        }
    }
    {% elif not attribute.is_static %}
    {{cpp_class}}* impl = {{v8_class}}::toImpl(holder);
    {% endif %}
    {% if interface_name == 'Window' and attribute.idl_type == 'EventHandler' %}
    if (!impl->document())
        return;
    {% endif %}
    {# Local variables #}
    {% if attribute.is_call_with_execution_context %}
    ExecutionContext* executionContext = currentExecutionContext(info.GetIsolate());
    {% endif %}
    {% if attribute.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% endif %}
    {% if attribute.is_check_security_for_node or
          attribute.is_getter_raises_exception %}
    ExceptionState exceptionState(ExceptionState::GetterContext, "{{attribute.name}}", "{{interface_name}}", holder, info.GetIsolate());
    {% endif %}
    {% if attribute.is_explicit_nullable %}
    bool isNull = false;
    {% endif %}
    {% if attribute.is_implemented_in_private_script %}
    {{attribute.cpp_type}} result{{attribute.cpp_type_initializer}};
    if (!{{attribute.cpp_value_original}})
        return;
    {% elif attribute.cpp_value_original %}
    {{attribute.cpp_type}} {{attribute.cpp_value}}({{attribute.cpp_value_original}});
    {% endif %}
    {# Checks #}
    {% if attribute.is_getter_raises_exception %}
    if (UNLIKELY(exceptionState.throwIfNeeded()))
        return;
    {% endif %}
    {% if attribute.is_check_security_for_node %}
    if (!BindingSecurity::shouldAllowAccessToNode(info.GetIsolate(), {{attribute.cpp_value}}, exceptionState)) {
        v8SetReturnValueNull(info);
        exceptionState.throwIfNeeded();
        return;
    }
    {% endif %}
    {% if attribute.reflect_only %}
    {{release_only_check(attribute.reflect_only, attribute.reflect_missing,
                         attribute.reflect_invalid, attribute.reflect_empty,
                         attribute.cpp_value)
      | indent}}
    {% endif %}
    {% if attribute.is_explicit_nullable %}
    if (isNull) {
        v8SetReturnValueNull(info);
        return;
    }
    {% endif %}
    {% if attribute.cached_attribute_validation_method %}
    V8HiddenValue::setHiddenValue(info.GetIsolate(), holder, propertyName, {{attribute.cpp_value_to_v8_value}});
    {% endif %}
    {# v8SetReturnValue #}
    {% if attribute.is_keep_alive_for_gc %}
    if ({{attribute.cpp_value}} && DOMDataStore::setReturnValue{{world_suffix}}(info.GetReturnValue(), {{attribute.cpp_value}}.get()))
        return;
    v8::Local<v8::Value> wrapper = toV8({{attribute.cpp_value}}.get(), holder, info.GetIsolate());
    if (!wrapper.IsEmpty()) {
        V8HiddenValue::setHiddenValue(info.GetIsolate(), holder, v8AtomicString(info.GetIsolate(), "{{attribute.name}}"), wrapper);
        {{attribute.v8_set_return_value}};
    }
    {% elif world_suffix %}
    {{attribute.v8_set_return_value_for_main_world}};
    {% else %}
    {% if attribute.use_output_parameter_for_result %}
    {{attribute.cpp_type}} result;
    {{attribute.cpp_value}};
    {% endif %}
    {{attribute.v8_set_return_value}};
    {% endif %}
}
{% endfilter %}
{% endmacro %}

{######################################}
{% macro release_only_check(reflect_only_values, reflect_missing,
                            reflect_invalid, reflect_empty, cpp_value) %}
{# Attribute is limited to only known values: check that the attribute value is
   one of those. If not, set it to the empty string.
   http://www.whatwg.org/specs/web-apps/current-work/#limited-to-only-known-values #}
{% if reflect_empty %}
if ({{cpp_value}}.isNull()) {
{% if reflect_missing %}
    {{cpp_value}} = "{{reflect_missing}}";
{% else %}
    ;
{% endif %}
} else if ({{cpp_value}}.isEmpty()) {
    {{cpp_value}} = "{{reflect_empty}}";
{% else %}
if ({{cpp_value}}.isEmpty()) {
{# FIXME: should use [ReflectEmpty] instead; need to change IDL files #}
{% if reflect_missing %}
    {{cpp_value}} = "{{reflect_missing}}";
{% else %}
    ;
{% endif %}
{% endif %}
{% for value in reflect_only_values %}
} else if (equalIgnoringCase({{cpp_value}}, "{{value}}")) {
    {{cpp_value}} = "{{value}}";
{% endfor %}
} else {
    {{cpp_value}} = "{{reflect_invalid}}";
}
{% endmacro %}


{##############################################################################}
{% macro attribute_getter_callback(attribute, world_suffix) %}
{% filter conditional(attribute.conditional_string) %}
static void {{attribute.name}}AttributeGetterCallback{{world_suffix}}(
{%- if attribute.is_expose_js_accessors %}
const v8::FunctionCallbackInfo<v8::Value>& info
{%- else %}
v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info
{%- endif %})
{
    TRACE_EVENT_SET_SAMPLING_STATE("blink", "DOMGetter");
    {% if attribute.deprecate_as %}
    UseCounter::countDeprecationIfNotPrivateScript(info.GetIsolate(), callingExecutionContext(info.GetIsolate()), UseCounter::{{attribute.deprecate_as}});
    {% endif %}
    {% if attribute.measure_as %}
    UseCounter::countIfNotPrivateScript(info.GetIsolate(), callingExecutionContext(info.GetIsolate()), UseCounter::{{attribute.measure_as('AttributeGetter')}});
    {% endif %}
    {% if world_suffix in attribute.activity_logging_world_list_for_getter %}
    ScriptState* scriptState = ScriptState::from(info.GetIsolate()->GetCurrentContext());
    V8PerContextData* contextData = scriptState->perContextData();
    {% if attribute.activity_logging_world_check %}
    if (scriptState->world().isIsolatedWorld() && contextData && contextData->activityLogger())
    {% else %}
    if (contextData && contextData->activityLogger())
    {% endif %}
        contextData->activityLogger()->logGetter("{{interface_name}}.{{attribute.name}}");
    {% endif %}
    {% if attribute.has_custom_getter %}
    {{v8_class}}::{{attribute.name}}AttributeGetterCustom(info);
    {% else %}
    {{cpp_class_or_partial}}V8Internal::{{attribute.name}}AttributeGetter{{world_suffix}}(info);
    {% endif %}
    TRACE_EVENT_SET_SAMPLING_STATE("v8", "V8Execution");
}
{% endfilter %}
{% endmacro %}


{##############################################################################}
{% macro constructor_getter_callback(attribute, world_suffix) %}
{% filter conditional(attribute.conditional_string) %}
static void {{attribute.name}}ConstructorGetterCallback{{world_suffix}}(
{%- if attribute.is_expose_js_accessors %}
const v8::FunctionCallbackInfo<v8::Value>& info
{%- else %}
v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info
{%- endif %})
{
    TRACE_EVENT_SET_SAMPLING_STATE("blink", "DOMGetter");
    {% if attribute.deprecate_as %}
    UseCounter::countDeprecationIfNotPrivateScript(info.GetIsolate(), callingExecutionContext(info.GetIsolate()), UseCounter::{{attribute.deprecate_as}});
    {% endif %}
    {% if attribute.measure_as %}
    UseCounter::countIfNotPrivateScript(info.GetIsolate(), callingExecutionContext(info.GetIsolate()), UseCounter::{{attribute.measure_as('ConstructorGetter')}});
    {% endif %}
    {% if attribute.is_expose_js_accessors %}
    v8ConstructorAttributeGetterAsAccessor(info);
    {% else %}
    v8ConstructorAttributeGetterAsProperty(property, info);
    {% endif %}
    TRACE_EVENT_SET_SAMPLING_STATE("v8", "V8Execution");
}
{% endfilter %}
{% endmacro %}


{##############################################################################}
{% macro attribute_setter(attribute, world_suffix) %}
{% filter conditional(attribute.conditional_string) %}
static void {{attribute.name}}AttributeSetter{{world_suffix}}(
{%- if attribute.is_expose_js_accessors %}
v8::Local<v8::Value> v8Value, const v8::FunctionCallbackInfo<v8::Value>& info
{%- else %}
v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<void>& info
{%- endif %})
{
    {% if attribute.is_reflect and attribute.idl_type == 'DOMString'
          and is_node and not attribute.is_implemented_in_private_script %}
    {% set cpp_class, v8_class = 'Element', 'V8Element' %}
    {% endif %}
    {# Local variables #}
    {% if not attribute.is_static and
          not attribute.is_replaceable and
          not attribute.constructor_type %}
    v8::Local<v8::Object> holder = info.Holder();
    {% endif %}
    {% if attribute.has_setter_exception_state %}
    ExceptionState exceptionState(ExceptionState::SetterContext, "{{attribute.name}}", "{{interface_name}}", holder, info.GetIsolate());
    {% endif %}
    {% if attribute.is_replaceable or
          attribute.constructor_type %}
    v8::Local<v8::String> propertyName = v8AtomicString(info.GetIsolate(), "{{attribute.name}}");
    {% endif %}
    {# impl #}
    {% if attribute.is_put_forwards %}
    {{cpp_class}}* proxyImpl = {{v8_class}}::toImpl(holder);
    {{attribute.cpp_type}} impl = WTF::getPtr(proxyImpl->{{attribute.name}}());
    if (!impl)
        return;
    {% elif not attribute.is_static and
            not attribute.is_replaceable and
            not attribute.constructor_type %}
    {{cpp_class}}* impl = {{v8_class}}::toImpl(holder);
    {% endif %}
    {% if attribute.idl_type == 'EventHandler' and interface_name == 'Window' %}
    if (!impl->document())
        return;
    {% endif %}
    {# Convert JS value to C++ value #}
    {% if attribute.idl_type != 'EventHandler' %}
    {% if v8_value_to_local_cpp_value(attribute) %}
    {{v8_value_to_local_cpp_value(attribute) | indent}}
    {% endif %}
    {% elif not is_node %}{# EventHandler hack #}
    moveEventListenerToNewWrapper(info.GetIsolate(), holder, {{attribute.event_handler_getter_expression}}, v8Value, {{v8_class}}::eventListenerCacheIndex);
    {% endif %}
    {# Type checking, possibly throw a TypeError, per:
       http://www.w3.org/TR/WebIDL/#es-type-mapping #}
    {% if attribute.has_type_checking_interface %}
    {# Type checking for interface types (if interface not implemented, throw
       TypeError), per http://www.w3.org/TR/WebIDL/#es-interface #}
    if (!cppValue{% if attribute.is_nullable %} && !isUndefinedOrNull(v8Value){% endif %}) {
        exceptionState.throwTypeError("The provided value is not of type '{{attribute.idl_type}}'.");
        exceptionState.throwIfNeeded();
        return;
    }
    {% elif attribute.enum_validation_expression %}
    {# Setter ignores invalid enum values:
       http://www.w3.org/TR/WebIDL/#idl-enums #}
    String string = cppValue;
    if (!({{attribute.enum_validation_expression}}))
        return;
    {% endif %}
    {# Pre-set context #}
    {% if attribute.is_custom_element_callbacks or
          (attribute.is_reflect and
           not(attribute.idl_type == 'DOMString' and is_node)) %}
    {# Skip on compact node DOMString getters #}
    CustomElementProcessingStack::CallbackDeliveryScope deliveryScope;
    {% endif %}
    {% if attribute.is_call_with_execution_context or
          attribute.is_setter_call_with_execution_context %}
    ExecutionContext* executionContext = currentExecutionContext(info.GetIsolate());
    {% endif %}
    {# Set #}
    {% if attribute.cpp_setter %}
    {{attribute.cpp_setter}};
    {% endif %}
    {# Post-set #}
    {% if attribute.is_setter_raises_exception %}
    exceptionState.throwIfNeeded();
    {% endif %}
    {% if attribute.cached_attribute_validation_method %}
    V8HiddenValue::deleteHiddenValue(info.GetIsolate(), holder, v8AtomicString(info.GetIsolate(), "{{attribute.name}}")); // Invalidate the cached value.
    {% endif %}
}
{% endfilter %}
{% endmacro %}


{##############################################################################}
{% macro attribute_setter_callback(attribute, world_suffix) %}
{% filter conditional(attribute.conditional_string) %}
static void {{attribute.name}}AttributeSetterCallback{{world_suffix}}(
{%- if attribute.is_expose_js_accessors %}
const v8::FunctionCallbackInfo<v8::Value>& info
{%- else %}
v8::Local<v8::String>, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<void>& info
{%- endif %})
{
    {% if attribute.is_expose_js_accessors %}
    v8::Local<v8::Value> v8Value = info[0];
    {% endif %}
    TRACE_EVENT_SET_SAMPLING_STATE("blink", "DOMSetter");
    {% if attribute.deprecate_as %}
    UseCounter::countDeprecationIfNotPrivateScript(info.GetIsolate(), callingExecutionContext(info.GetIsolate()), UseCounter::{{attribute.deprecate_as}});
    {% endif %}
    {% if attribute.measure_as %}
    UseCounter::countIfNotPrivateScript(info.GetIsolate(), callingExecutionContext(info.GetIsolate()), UseCounter::{{attribute.measure_as('AttributeSetter')}});
    {% endif %}
    {% if world_suffix in attribute.activity_logging_world_list_for_setter %}
    ScriptState* scriptState = ScriptState::from(info.GetIsolate()->GetCurrentContext());
    V8PerContextData* contextData = scriptState->perContextData();
    {% if attribute.activity_logging_world_check %}
    if (scriptState->world().isIsolatedWorld() && contextData && contextData->activityLogger()) {
    {% else %}
    if (contextData && contextData->activityLogger()) {
    {% endif %}
        contextData->activityLogger()->logSetter("{{interface_name}}.{{attribute.name}}", v8Value);
    }
    {% endif %}
    {% if attribute.is_custom_element_callbacks or attribute.is_reflect %}
    CustomElementProcessingStack::CallbackDeliveryScope deliveryScope;
    {% endif %}
    {% if attribute.has_custom_setter %}
    {{v8_class}}::{{attribute.name}}AttributeSetterCustom(v8Value, info);
    {% else %}
    {{cpp_class_or_partial}}V8Internal::{{attribute.name}}AttributeSetter{{world_suffix}}(v8Value, info);
    {% endif %}
    TRACE_EVENT_SET_SAMPLING_STATE("v8", "V8Execution");
}
{% endfilter %}
{% endmacro %}


{##############################################################################}
{% macro attribute_getter_implemented_in_private_script(attribute) %}
bool {{v8_class}}::PrivateScript::{{attribute.name}}AttributeGetter(LocalFrame* frame, {{cpp_class}}* holderImpl, {{attribute.cpp_type}}* result)
{
    if (!frame)
        return false;
    v8::HandleScope handleScope(toIsolate(frame));
    ScriptForbiddenScope::AllowUserAgentScript script;
    v8::Local<v8::Context> contextInPrivateScript = toV8Context(frame, DOMWrapperWorld::privateScriptIsolatedWorld());
    if (contextInPrivateScript.IsEmpty())
        return false;
    ScriptState* scriptState = ScriptState::from(contextInPrivateScript);
    ScriptState* scriptStateInUserScript = ScriptState::forMainWorld(frame);
    if (!scriptState->executionContext())
        return false;

    ScriptState::Scope scope(scriptState);
    v8::Local<v8::Value> holder = toV8(holderImpl, scriptState->context()->Global(), scriptState->isolate());

    ExceptionState exceptionState(ExceptionState::GetterContext, "{{attribute.name}}", "{{cpp_class}}", scriptState->context()->Global(), scriptState->isolate());
    v8::Local<v8::Value> v8Value = PrivateScriptRunner::runDOMAttributeGetter(scriptState, scriptStateInUserScript, "{{cpp_class}}", "{{attribute.name}}", holder);
    if (v8Value.IsEmpty())
        return false;
    {{v8_value_to_local_cpp_value(attribute.private_script_v8_value_to_local_cpp_value) | indent}}
    RELEASE_ASSERT(!exceptionState.hadException());
    *result = cppValue;
    return true;
}
{% endmacro %}


{% macro attribute_setter_implemented_in_private_script(attribute) %}
bool {{v8_class}}::PrivateScript::{{attribute.name}}AttributeSetter(LocalFrame* frame, {{cpp_class}}* holderImpl, {{attribute.argument_cpp_type}} cppValue)
{
    if (!frame)
        return false;
    v8::HandleScope handleScope(toIsolate(frame));
    ScriptForbiddenScope::AllowUserAgentScript script;
    v8::Local<v8::Context> contextInPrivateScript = toV8Context(frame, DOMWrapperWorld::privateScriptIsolatedWorld());
    if (contextInPrivateScript.IsEmpty())
        return false;
    ScriptState* scriptState = ScriptState::from(contextInPrivateScript);
    ScriptState* scriptStateInUserScript = ScriptState::forMainWorld(frame);
    if (!scriptState->executionContext())
        return false;

    ScriptState::Scope scope(scriptState);
    v8::Local<v8::Value> holder = toV8(holderImpl, scriptState->context()->Global(), scriptState->isolate());

    ExceptionState exceptionState(ExceptionState::SetterContext, "{{attribute.name}}", "{{cpp_class}}", scriptState->context()->Global(), scriptState->isolate());
    return PrivateScriptRunner::runDOMAttributeSetter(scriptState, scriptStateInUserScript, "{{cpp_class}}", "{{attribute.name}}", holder, {{attribute.private_script_cpp_value_to_v8_value}});
}
{% endmacro %}


{##############################################################################}
{% macro attribute_configuration(attribute) %}
{% set getter_callback =
       '%sV8Internal::%sAttributeGetterCallback' %
           (cpp_class_or_partial, attribute.name)
       if not attribute.constructor_type else (
       '%sV8Internal::%sConstructorGetterCallback' %
           (cpp_class_or_partial, attribute.name)
       if attribute.needs_constructor_getter_callback else (
       'v8ConstructorAttributeGetterAsAccessor'
       if attribute.is_expose_js_accessors else (
       'v8ConstructorAttributeGetterAsProperty'))) %}
{% set getter_callback_for_main_world =
       '%sV8Internal::%sAttributeGetterCallbackForMainWorld' %
           (cpp_class_or_partial, attribute.name)
       if attribute.is_per_world_bindings else '0' %}
{% set setter_callback =
       '%sV8Internal::%sAttributeSetterCallback' %
           (cpp_class_or_partial, attribute.name)
       if attribute.has_setter else '0' %}
{% set setter_callback_for_main_world =
       '%sV8Internal::%sAttributeSetterCallbackForMainWorld' %
           (cpp_class_or_partial, attribute.name)
       if attribute.is_per_world_bindings and attribute.has_setter else '0' %}
{% set wrapper_type_info =
       'const_cast<WrapperTypeInfo*>(&V8%s::wrapperTypeInfo)' %
           attribute.constructor_type
       if attribute.constructor_type else '0' %}
{% set access_control = 'static_cast<v8::AccessControl>(%s)' %
                        ' | '.join(attribute.access_control_list) %}
{% set property_attribute = 'static_cast<v8::PropertyAttribute>(%s)' %
                            ' | '.join(attribute.property_attributes) %}
{% set only_exposed_to_private_script = 'V8DOMConfiguration::OnlyExposedToPrivateScript' if attribute.only_exposed_to_private_script else 'V8DOMConfiguration::ExposedToAllScripts' %}
{% set on_prototype = 'V8DOMConfiguration::OnPrototype'
       if interface_name == 'Window' and attribute.idl_type == 'EventHandler'
       else 'V8DOMConfiguration::OnInstance' %}
{% set attribute_configuration_list = [
       '"%s"' % attribute.name,
       getter_callback,
       setter_callback,
       getter_callback_for_main_world,
       setter_callback_for_main_world,
       wrapper_type_info,
       access_control,
       property_attribute,
       only_exposed_to_private_script,
   ] %}
{% if not attribute.is_expose_js_accessors %}
{% set attribute_configuration_list = attribute_configuration_list
                                    + [on_prototype] %}
{% endif %}
{{'{'}}{{attribute_configuration_list | join(', ')}}{{'}'}}
{%- endmacro %}
