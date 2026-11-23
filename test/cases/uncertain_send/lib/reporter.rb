class Reporter
  def report = nil

  def legacy(kind)
    send(kind)              # inside dead code: never runs, so it makes nothing uncertain
  end

  def summary = nil         # dead

  def forward(target, kind)
    target.send(kind)       # another receiver: says nothing about Reporter
  end
end
