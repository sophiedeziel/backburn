class OrderTest
  def test_tested_only
    assert_equal 1, Order.new([]).tested_only
  end
end
