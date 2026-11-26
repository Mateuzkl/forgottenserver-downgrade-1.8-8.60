local event = Event()
function event.onTargetCombat(self, target)
    if not self then
        return RETURNVALUE_NOERROR
    end
    return RETURNVALUE_NOERROR
end

event:register()
